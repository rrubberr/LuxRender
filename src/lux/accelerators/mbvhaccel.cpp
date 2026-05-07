/***************************************************************************
 *   Copyright (C) 1998-2013 by authors (see AUTHORS.txt)                  *
 *                                                                         *
 *   This file is part of LuxRender.                                       *
 *                                                                         *
 *   Lux Renderer is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   Lux Renderer is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. *
 *                                                                         *
 *   This project is based on PBRT ; see http://www.pbrt.org               *
 *   Lux Renderer website : http://www.luxrender.org                       *
 ***************************************************************************/

// ---------------------------------------------------------------------------
// MBVH accelerator
// ---------------------------------------------------------------------------
// Construction pipeline (Wald 2008, "Stackless Multi-BVH Traversal"):
//   1. Build a standard SAH binary BVH over the primitives.
//   2. Annotate each binary node with its SAH collapse cost.
//   3. Collapse the binary tree into a wide BVH.
//   4. Lay out the resulting wide nodes in a depth-first flat array.
// ---------------------------------------------------------------------------

// mbvhaccel.cpp*

#include "mbvhaccel.h"
#include "paramset.h"
#include "dynload.h"
#include "error.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>

using namespace luxrays;
using namespace lux;

// ---------------------------------------------------------------------------
// Helpers.
// ---------------------------------------------------------------------------

static inline float BBoxSurfaceArea(const BBox &b)
{
	Vector d = b.pMax - b.pMin;
	return 2.f * (d.x * d.y + d.x * d.z + d.y * d.z);
}

// ---------------------------------------------------------------------------
// Constructor / Destructor.
// ---------------------------------------------------------------------------

MBVHAccel::MBVHAccel(const vector<boost::shared_ptr<Primitive> > &p,
		int csamples, int icost, int tcost, float ebonus, int maxleafp)
	: costSamples(csamples), isectCost(icost), traversalCost(tcost),
	  maxLeafPrims(maxleafp < 1 ? 1 : maxleafp),
	  emptyBonus(ebonus), nPrims(0), prims(nullptr), wideNodes(nullptr), nWideNodes(0)
{
	// ------------------------------------------------------------------
	// Collect intersectable primitives.
	// ------------------------------------------------------------------
	vector<boost::shared_ptr<Primitive> > vPrims;
	const PrimitiveRefinementHints refineHints(false);
	for (u_int i = 0; i < p.size(); ++i) {
		if (p[i]->CanIntersect())
			vPrims.push_back(p[i]);
		else
			p[i]->Refine(vPrims, refineHints, p[i]);
	}

	nPrims = static_cast<u_int>(vPrims.size());
	if (nPrims == 0)
		return;

	// Keep ownership of the original shared_ptr array.
	prims = AllocAligned<boost::shared_ptr<Primitive> >(nPrims);
	for (u_int i = 0; i < nPrims; ++i)
		new (&prims[i]) boost::shared_ptr<Primitive>(vPrims[i]);

	// Create per-primitive leaf nodes for the binary build.
	vector<boost::shared_ptr<BVHAccelTreeNode> > leafNodes(nPrims);
	for (u_int i = 0; i < nPrims; ++i) {
		BVHAccelTreeNode *ln = new BVHAccelTreeNode();
		ln->bbox      = prims[i]->WorldBound();
		ln->bbox.Expand(MachineEpsilon::E(ln->bbox));
		ln->isLeaf    = true;
		ln->primitive = prims[i].get();
		buildNodes.push_back(ln); // Non-owning shared_ptr: lifetime is managed by buildNodes.
		leafNodes[i] = boost::shared_ptr<BVHAccelTreeNode>(ln, [](BVHAccelTreeNode *) {});
	}

	LOG(LUX_INFO, LUX_NOERROR) << "Building binary BVH with " << nPrims << " primitives, " 
		<< maxLeafPrims << " binary leaf prims, and "
		<< (MBVH_WIDTH * maxLeafPrims) << " wide leaf capacity." ;

	// Build binary BVH.
	BVHAccelTreeNode *root = BuildBinaryBVH(leafNodes, 0, nPrims);

	// Annotate nodes with SAH cost.
	float rootSA    = BBoxSurfaceArea(root->bbox);
	float rootSAInv = (rootSA > 0.f) ? 1.f / rootSA : 0.f;
	ComputeCollapseInfo(root, rootSAInv);

	// Collapse binary tree.
	LOG(LUX_INFO, LUX_NOERROR) << "Collapsing binary BVH to " << MBVH_WIDTH << "-wide Multi-BVH";

	vector<MBVHNode> wideNodesTmp;
	wideNodesTmp.reserve(nPrims);
	CollapseToWide(root, leafNodes, wideNodesTmp, orderedPrims);

	nWideNodes = static_cast<u_int>(wideNodesTmp.size());
	wideNodes = AllocAligned<MBVHNode>(nWideNodes, MBVH_ALIGN);
	for (u_int i = 0; i < nWideNodes; ++i)
		new (&wideNodes[i]) MBVHNode(wideNodesTmp[i]);

	// Compute and log quality statistics
	// This writes slot counts and outputs fill histogram.
	u_int totalFilled = 0, totalLeafSlots = 0, totalInnerSlots = 0;
	u_int fillHist[MBVH_WIDTH + 1] = {}; // fillHist[k] = #nodes with k filled slots.
	for (u_int i = 0; i < nWideNodes; ++i) {
		int nodeFill = 0;
		for (int s = 0; s < MBVH_WIDTH; ++s) {
			int ci = wideNodesTmp[i].childIndex[s];
			if (ci == MBVH_EMPTY_CHILD) continue;
			++nodeFill;
			++totalFilled;
			if (ci < 0) ++totalLeafSlots;
			else        ++totalInnerSlots;
		}
		fillHist[nodeFill]++;
	}
	float avgFill = (nWideNodes > 0)
		? static_cast<float>(totalFilled) / static_cast<float>(nWideNodes)
		: 0.f;

	// depth pass: iterative DFS over the wide node array.
	u_int maxDepth       = 0;	// max depth of any leaf slot.
	u_int totalLeafDepth = 0;   // sum of depth at every leaf slot.
	u_int leafDepthCount = 0;   // number of leaf slots visited.

	{
		// Stack entries: (nodeIndex, depth).
		vector<std::pair<u_int, u_int> > dfsStack;
		dfsStack.reserve(nWideNodes);
		dfsStack.push_back(std::make_pair(0u, 1u));
		while (!dfsStack.empty()) {
			u_int ni    = dfsStack.back().first;
			u_int depth = dfsStack.back().second;
			dfsStack.pop_back();
			if (depth > maxDepth) maxDepth = depth;
			for (int s = 0; s < MBVH_WIDTH; ++s) {
				int ci = wideNodesTmp[ni].childIndex[s];
				if (ci == MBVH_EMPTY_CHILD) continue;
				if (ci < 0) {
					totalLeafDepth += depth;
					++leafDepthCount;
				} else {
					dfsStack.push_back(std::make_pair(static_cast<u_int>(ci), depth + 1u));
				}
			}
		}
	}
	float avgLeafDepth = (leafDepthCount > 0)
		? static_cast<float>(totalLeafDepth) / static_cast<float>(leafDepthCount)
		: 0.f;

	// Free the binary build tree.
	for (BVHAccelTreeNode *n : buildNodes)
		delete n;
	buildNodes.clear();

	LOG(LUX_INFO, LUX_NOERROR)
		<< "Finished " << MBVH_WIDTH << "-wide BVH:"
		<< " nodes: "        << nWideNodes
		<< ", inner slots: " << totalInnerSlots
		<< ", leaf slots: "  << totalLeafSlots
		<< ", avg fill: "    << std::fixed << std::setprecision(2) << avgFill
		<< "/" << MBVH_WIDTH
		<< " (" << std::fixed << std::setprecision(1)
		<< (100.f * avgFill / MBVH_WIDTH) << "%)"
		<< ", depth max/avg: " << maxDepth
		<< "/" << std::fixed << std::setprecision(1) << avgLeafDepth;

	// Fill histogram: only print non-zero buckets.
	{
		std::ostringstream hist;
		hist << "Fill histogram (slots:nodes):";
		for (int k = 1; k <= MBVH_WIDTH; ++k) {
			if (fillHist[k] > 0)
				hist << " " << k << ":" << fillHist[k];
		}
		LOG(LUX_INFO, LUX_NOERROR) << hist.str();
	}
}

MBVHAccel::~MBVHAccel()
{
	for (u_int i = 0; i < nPrims; ++i)
		prims[i].~shared_ptr();
	FreeAligned(prims);
	if (wideNodes) {
		for (u_int i = 0; i < nWideNodes; ++i)
			wideNodes[i].~MBVHNode();
		FreeAligned(wideNodes);
	}
}

// Binary SAH BVH construction.
//
// With the default maxLeafPrims = 1 this builds a standard 1-prim-per-leaf
// SAH binary BVH: the only base case is a single-element range, which reuses
// the pre-allocated leaf node directly. The multi-prim aggregate leaf path
// (end - begin > 1 but still <= maxLeafPrims) is reachable only when the
// caller has explicitly set maxLeafPrims > 1.
//
// Every node (both leaves and inner nodes) is stamped with
// [leafPrimStart, leafPrimEnd) covering its entire primitive subtree.
// CollapseToWide reads this range in O(1) to decide whether to inline the
// node's whole subtree as a single fat leaf slot without recursing.
BVHAccelTreeNode *MBVHAccel::BuildBinaryBVH(
		vector<boost::shared_ptr<BVHAccelTreeNode> > &leaves,
		u_int begin, u_int end)
{
	assert(begin < end);

	// Base case: range small enough for a single binary leaf node.
	if (end - begin <= static_cast<u_int>(maxLeafPrims)) {
		if (end - begin == 1) {
			// Reuse the pre-created single-prim leaf and stamp its range.
			// This is the only path taken when maxLeafPrims == 1 (default).
			BVHAccelTreeNode *leaf = leaves[begin].get();
			leaf->leafPrimStart = begin;
			leaf->leafPrimEnd   = end;
			return leaf;
		}

		// Multi-prim aggregate leaf: only reachable when maxLeafPrims > 1.
		// Covers all primitives in [begin, end) under one node; primitive == nullptr
		// distinguishes this from a single-prim leaf.
		BVHAccelTreeNode *node = new BVHAccelTreeNode();
		buildNodes.push_back(node);
		node->isLeaf        = true;
		node->primitive     = nullptr;
		node->leafPrimStart = begin;
		node->leafPrimEnd   = end;
		node->bbox = leaves[begin]->bbox;
		for (u_int i = begin + 1; i < end; ++i)
			node->bbox = Union(node->bbox, leaves[i]->bbox);
		return node;
	}

	u_int bestAxis;
	float splitValue;
	FindBestSplit(leaves, begin, end, &splitValue, &bestAxis);

	// Partition around splitValue on bestAxis.
	auto mid = std::partition(
		leaves.begin() + begin,
		leaves.begin() + end,
		[bestAxis, splitValue](const boost::shared_ptr<BVHAccelTreeNode> &n) {
			return (n->bbox.pMax[bestAxis] + n->bbox.pMin[bestAxis]) < splitValue;
		});

	u_int midIdx = static_cast<u_int>(std::distance(leaves.begin(), mid));
	// Guard against degenerate partition.
	if (midIdx <= begin) midIdx = begin + 1;
	if (midIdx >= end)   midIdx = end - 1;

	BVHAccelTreeNode *node = new BVHAccelTreeNode();
	buildNodes.push_back(node);
	node->isLeaf           = false;
	node->children.left    = BuildBinaryBVH(leaves, begin,  midIdx);
	node->children.right   = BuildBinaryBVH(leaves, midIdx, end);
	node->bbox = Union(node->children.left->bbox, node->children.right->bbox);
	// Stamp the subtree range on inner nodes so CollapseToWide can decide
	// whether to inline the whole subtree as one fat leaf slot without an
	// extra traversal.
	node->leafPrimStart = begin;
	node->leafPrimEnd   = end;

	return node;
}

void MBVHAccel::FindBestSplit(
		vector<boost::shared_ptr<BVHAccelTreeNode> > &list,
		u_int begin, u_int end,
		float *splitValue, u_int *bestAxis)
{
	if (end - begin == 2) {
		*splitValue = (list[begin]->bbox.pMax[0] + list[begin]->bbox.pMin[0] +
		               list[end-1]->bbox.pMax[0] + list[end-1]->bbox.pMin[0]) * 0.5f;
		*bestAxis   = 0;
		return;
	}

	if (costSamples > 1) {
		
		// Compute both object bounds (for surface area) and centroid bounds
		// (for split planes). Realistically, costSamples should ALWAYS be > 1.
		BBox nodeBounds, centroidBounds;
		for (u_int i = begin; i < end; ++i) {
			nodeBounds = Union(nodeBounds, list[i]->bbox);
			Point c = (list[i]->bbox.pMin + list[i]->bbox.pMax) * 0.5f;
			centroidBounds = Union(centroidBounds, c);
		}

		float totalSA    = BBoxSurfaceArea(nodeBounds);
		float invTotalSA = (totalSA > 0.f) ? 1.f / totalSA : 0.f;

		float bestCost = std::numeric_limits<float>::infinity();
		*bestAxis   = 0;
		*splitValue = 0.f;

		Vector cext = centroidBounds.pMax - centroidBounds.pMin;

		// Test all three axes and take the globally best SAH split.
		for (u_int axis = 0; axis < 3; ++axis) {
			if (cext[axis] <= 0.f) continue; // all centroids coincide on this axis.
			
			// Sample candidate planes evenly within the centroid range.
			// splitValue is in "2*centroid" space to match the partition
			// comparison (pMax[axis] + pMin[axis]) < splitValue.
			float increment = cext[axis] / static_cast<float>(costSamples + 1);
			for (int s = 1; s <= costSamples; ++s) {
				float splitVal = 2.f * (centroidBounds.pMin[axis] +
				                        s * increment);

				int   nBelow = 0, nAbove = 0;
				BBox  bbBelow, bbAbove;
				for (u_int j = begin; j < end; ++j) {
					if ((list[j]->bbox.pMax[axis] + list[j]->bbox.pMin[axis]) < splitVal) {
						++nBelow;
						bbBelow = Union(bbBelow, list[j]->bbox);
					} else {
						++nAbove;
						bbAbove = Union(bbAbove, list[j]->bbox);
					}
				}
				float belowSA = BBoxSurfaceArea(bbBelow);
				float aboveSA = BBoxSurfaceArea(bbAbove);
				float pBelow  = belowSA * invTotalSA;
				float pAbove  = aboveSA * invTotalSA;
				float eb      = (nAbove == 0 || nBelow == 0) ? emptyBonus : 0.f;
				float cost    = traversalCost +
				                isectCost * (1.f - eb) * (pBelow * nBelow + pAbove * nAbove);
				if (cost < bestCost) {
					bestCost    = cost;
					*bestAxis   = axis;
					*splitValue = splitVal;
				}
			}
		}

		if (bestCost < std::numeric_limits<float>::infinity())
			return;
		// All centroids identical; fall through.
	}

	// Centroid-median fallback (costSamples <= 1, or all axes degenerate):
	// split on the axis with highest centroid variance.
	Point mean2(0.f, 0.f, 0.f);
	Point var(0.f, 0.f, 0.f);
	for (u_int i = begin; i < end; ++i)
		mean2 += list[i]->bbox.pMax + list[i]->bbox.pMin;
	mean2 /= static_cast<float>(end - begin);

	for (u_int i = begin; i < end; ++i) {
		Vector v = list[i]->bbox.pMax + list[i]->bbox.pMin - mean2;
		v.x *= v.x; v.y *= v.y; v.z *= v.z;
		var += v;
	}

	if (var.x > var.y && var.x > var.z)  *bestAxis = 0;
	else if (var.y > var.z)              *bestAxis = 1;
	else                                  *bestAxis = 2;
	*splitValue = mean2[*bestAxis];
}

// ---------------------------------------------------------------------------
// SAH collapse cost annotation (Wald 2008, Section 3).
// ---------------------------------------------------------------------------
// C_leaf(n)  = isectCost * 1 (one primitive per leaf in binary BVH)
// C_inner(n) = traversalCost
//            + SA(left)/SA(n)  * C(left)
//            + SA(right)/SA(n) * C(right)
// ---------------------------------------------------------------------------
// node->sahCost stores the per-unit-SA cost for the subtree rooted at node,
// i.e., sahCost = C(n)/SA(n). This makes the recursive formula:
// C_inner(n)/SA(n) = traversalCost/SA(n) + SA(left)*sahCost(left)/SA(n)
//                                          + SA(right)*sahCost(right)/SA(n)
// ---------------------------------------------------------------------------

float MBVHAccel::ComputeCollapseInfo(BVHAccelTreeNode *node, float rootSAInv)
{
	float sa = BBoxSurfaceArea(node->bbox);

	if (node->isLeaf) {
		// Cost scales with the number of primitives in this leaf.
		u_int n = (node->primitive != nullptr)
		        ? 1u
		        : (node->leafPrimEnd - node->leafPrimStart);
		node->sahCost = static_cast<float>(isectCost) * static_cast<float>(n);
		return sa * node->sahCost * rootSAInv;
	}

	ComputeCollapseInfo(node->children.left,  rootSAInv);
	ComputeCollapseInfo(node->children.right, rootSAInv);

	float saLeft  = BBoxSurfaceArea(node->children.left->bbox);
	float saRight = BBoxSurfaceArea(node->children.right->bbox);

	float innerCost = static_cast<float>(traversalCost)
	                + saLeft  * node->children.left->sahCost
	                + saRight * node->children.right->sahCost;

	node->sahCost = (sa > 0.f) ? innerCost / sa : 0.f;
	return sa * node->sahCost * rootSAInv;
}

// ---------------------------------------------------------------------------
// Child node gathering (Wald 2008, Section 4).
// Starting from a node, repeatedly replace the non-leaf candidate with the
// highest SAH cost contribution (surface-area*sahCost), by its two
// children, until we have filled all slots or all candidates are leaves.
// Using SA*sahCost rather than raw SA respects primitive density: a large
// but nearly-empty node is worth less than a smaller node that
// concentrates expensive primitives.
// ---------------------------------------------------------------------------

void MBVHAccel::GatherChildren(BVHAccelTreeNode *node, int maxChildren,
		vector<BVHAccelTreeNode *> &out)
{
	if (node->isLeaf) {
		out.push_back(node);
		return;
	}

	// Seed with the two direct children of this (non-leaf) binary node.
	out.push_back(node->children.left);
	out.push_back(node->children.right);

	while (static_cast<int>(out.size()) < maxChildren) {
		// Find the non-leaf candidate with the highest expected SAH cost
		// contribution: SA(n)*sahCost(n). sahCost encodes both traversal
		// cost and primitive density, so this correctly prefers dense,
		// costly subtrees over large-but-cheap ones.
		int   bestIdx  = -1;
		float bestCost = -1.f;
		for (int i = 0; i < static_cast<int>(out.size()); ++i) {
			if (!out[i]->isLeaf) {
				float cost = BBoxSurfaceArea(out[i]->bbox) * out[i]->sahCost;
				if (cost > bestCost) {
					bestCost = cost;
					bestIdx  = i;
				}
			}
		}

		if (bestIdx < 0)
			break; // All candidates are leaves; nothing more to do.

		// Replace this node with its two binary child nodes.
		BVHAccelTreeNode *toOpen = out[bestIdx];
		out[bestIdx] = toOpen->children.left;
		out.push_back(toOpen->children.right);
	}
}

// ---------------------------------------------------------------------------
// EmitSubtreeLeaves
// ---------------------------------------------------------------------------
// Appends every primitive covered by the binary subtree rooted at "node" to
// "oPrims."
//
// Handles all node combos produced by BuildBinaryBVH:
//   isLeaf && primitive != nullptr  -> single-prim leaf
//   isLeaf && primitive == nullptr  -> multi-prim aggregate leaf
//   isLeaf == false                 -> inner node: recurse left then right
// ---------------------------------------------------------------------------

static void EmitSubtreeLeaves(
		BVHAccelTreeNode *node,
		const vector<boost::shared_ptr<BVHAccelTreeNode> > &leafNodes,
		vector<Primitive *> &oPrims)
{
	if (node->isLeaf) {
		if (node->primitive != nullptr) {
			oPrims.push_back(node->primitive);
		} else {
			for (u_int pi = node->leafPrimStart; pi < node->leafPrimEnd; ++pi)
				oPrims.push_back(leafNodes[pi]->primitive);
		}
		return;
	}
	EmitSubtreeLeaves(node->children.left,  leafNodes, oPrims);
	EmitSubtreeLeaves(node->children.right, leafNodes, oPrims);
}

// ---------------------------------------------------------------------------
// GatheredChild
// ---------------------------------------------------------------------------
// Represents one slot in the intermediate gathered-children list maintained
// during CollapseToWide. After the initial GatherChildren call produces a
// list of raw binary-tree nodes, each node is wrapped in a GatheredChild
// and classified as either leaf-type or inner-type. The leaf-merge loop
// (Phase 3) may then combine two leaf-type entries into one, so a single
// GatheredChild can represent the union of several binary-tree nodes.
// ---------------------------------------------------------------------------

struct GatheredChild {
	BBox  bbox;
	u_int primCount;  // total primitives covered by this slot
	bool  isLeafType;

	// leaf-type: binary nodes whose subtrees to emit
	BVHAccelTreeNode *subNodes[MBVH_WIDTH];
	u_int             nSubNodes;

	// inner-type: single binary node to recurse into
	BVHAccelTreeNode *innerNode;
	float             innerSACost;  // SA(bbox) × sahCost
};

// ---------------------------------------------------------------------------
// LeafMergeGain (Wald 2008, Section 4.2)
// ---------------------------------------------------------------------------
// Computes the expected SAH cost reduction from merging two leaf slots
// (surface areas saA/saB, prim counts nA/nB) into a single slot whose
// bounding box is unionBBox.
// ---------------------------------------------------------------------------

static float LeafMergeGain(float saA, u_int nA,
                            float saB, u_int nB,
                            const BBox &unionBBox)
{
	const u_int W = static_cast<u_int>(MBVH_WIDTH);
	// Number of SIMD intersection chunks needed for n primitives.
	auto chunks = [W](u_int n) -> float {
		return static_cast<float>((n + W - 1u) / W);
	};
	return saA * chunks(nA) + saB * chunks(nB)
	     - BBoxSurfaceArea(unionBBox) * chunks(nA + nB);
}

// ---------------------------------------------------------------------------
// CollapseToWide
// ---------------------------------------------------------------------------
// Recursively collapses the binary BVH rooted at "node" into the flat wide
// node array. Returns the index of the MBVHNode created for "node."
// ---------------------------------------------------------------------------

u_int MBVHAccel::CollapseToWide(BVHAccelTreeNode *node,
		const vector<boost::shared_ptr<BVHAccelTreeNode> > &leafNodes,
		vector<MBVHNode> &wideNodes,
		vector<Primitive *> &oPrims)
{
	// Allocate a slot for this wide node up front.
	u_int nodeIdx = static_cast<u_int>(wideNodes.size());
	wideNodes.emplace_back();

	// -----------------------------------------------------------------------
	// Phase 1: gather up to MBVH_WIDTH binary-tree children.
	// -----------------------------------------------------------------------
	vector<BVHAccelTreeNode *> rawChildren;
	rawChildren.reserve(MBVH_WIDTH);
	GatherChildren(node, MBVH_WIDTH, rawChildren);

	// Inline threshold: a gathered binary inner node whose subtree covers at
	// most this many primitives is committed as a fat leaf slot rather than
	// recursed into.
	const u_int inlineThreshold = static_cast<u_int>(MBVH_WIDTH * maxLeafPrims);

	// -----------------------------------------------------------------------
	// Phase 2: classify each gathered binary node as leaf-type or inner-type.
	// -----------------------------------------------------------------------

	// Build a GatheredChild entry from one binary tree node.
	auto makeGathered = [&](BVHAccelTreeNode *ch) -> GatheredChild {
		GatheredChild gc;
		gc.bbox      = ch->bbox;
		gc.primCount = ch->leafPrimEnd - ch->leafPrimStart;

		if (ch->isLeaf || gc.primCount <= inlineThreshold) {
			// Leaf-type: commit as a single fat leaf slot.
			gc.isLeafType  = true;
			gc.subNodes[0] = ch;
			gc.nSubNodes   = 1;
			gc.innerNode   = nullptr;
			gc.innerSACost = 0.f;
		} else {
			// Inner-type: recurse into CollapseToWide later.
			gc.isLeafType  = false;
			gc.nSubNodes   = 0;
			gc.innerNode   = ch;
			gc.innerSACost = BBoxSurfaceArea(ch->bbox) * ch->sahCost;
		}
		return gc;
	};

	vector<GatheredChild> gathered;
	gathered.reserve(MBVH_WIDTH);
	for (BVHAccelTreeNode *ch : rawChildren)
		gathered.push_back(makeGathered(ch));

	// -----------------------------------------------------------------------
	// Phase 3: leaf-merge loop.
	// -----------------------------------------------------------------------
	// Invariant: gathered.size() does not change during the loop.
	// Each iteration: merge two leaf entries into one (-1), then replace the
	// highest-cost inner entry with its two binary children (+1); net zero.
	for (;;) {
		// (a) Find the highest-cost inner-type slot to expand into.
		//     If none exist, there is nothing to expand into; stop.
		int   bestInnerIdx  = -1;
		float bestInnerCost = -1.f;
		for (int i = 0; i < static_cast<int>(gathered.size()); ++i) {
			if (!gathered[i].isLeafType &&
			    gathered[i].innerSACost > bestInnerCost) {
				bestInnerCost = gathered[i].innerSACost;
				bestInnerIdx  = i;
			}
		}
		if (bestInnerIdx < 0)
			break; // No inner-type slots remain.

		// (b) Find the pair of leaf-type slots with the highest positive
		//     SAH merge gain. If no pair has gain > 0, stop.
		int   bestMergeI = -1, bestMergeJ = -1;
		float bestGain   = 0.f; // strictly positive required
		const int n = static_cast<int>(gathered.size());
		for (int i = 0; i < n; ++i) {
			if (!gathered[i].isLeafType) continue;
			const float saI = BBoxSurfaceArea(gathered[i].bbox);
			for (int j = i + 1; j < n; ++j) {
				if (!gathered[j].isLeafType) continue;
				BBox  ubb  = Union(gathered[i].bbox, gathered[j].bbox);
				float gain = LeafMergeGain(saI,              gathered[i].primCount,
				                           BBoxSurfaceArea(gathered[j].bbox),
				                           gathered[j].primCount, ubb);
				if (gain > bestGain) {
					bestGain   = gain;
					bestMergeI = i;
					bestMergeJ = j;
				}
			}
		}
		if (bestMergeI < 0)
			break; // No leaf pair with positive gain.

		// -------------------------------------------------------------------
		// Merge gathered[bestMergeI] and gathered[bestMergeJ].
		// bestMergeI is kept; bestMergeJ is absorbed into it, then removed.
		// -------------------------------------------------------------------
		GatheredChild &A = gathered[bestMergeI];
		const GatheredChild &B = gathered[bestMergeJ]; // read-only until removed

		A.bbox      = Union(A.bbox, B.bbox);
		A.primCount += B.primCount;
		for (u_int k = 0; k < B.nSubNodes; ++k) {
			assert(A.nSubNodes < static_cast<u_int>(MBVH_WIDTH) &&
			       "leaf-merge subNode overflow: more than MBVH_WIDTH nodes merged");
			A.subNodes[A.nSubNodes++] = B.subNodes[k];
		}
		// A.isLeafType remains true.

		// Remove bestMergeJ via swap-with-last-and-pop (O(1)).
		// If bestInnerIdx points to the last element it will move to bestMergeJ.
		const int lastBeforePop = static_cast<int>(gathered.size()) - 1;
		if (bestMergeJ != lastBeforePop) {
			if (bestInnerIdx == lastBeforePop)
				bestInnerIdx = bestMergeJ; // track where it moved
			gathered[bestMergeJ] = gathered[lastBeforePop];
		}
		gathered.pop_back();
		// gathered.size() is now n - 1.

		// -------------------------------------------------------------------
		// Expand the best inner-type slot (bestInnerIdx) into its two binary
		// children, using the slot freed by the merge above.
		// -------------------------------------------------------------------
		BVHAccelTreeNode *toExpand = gathered[bestInnerIdx].innerNode;

		// Remove bestInnerIdx via swap-with-last-and-pop.
		const int lastBeforeExpand = static_cast<int>(gathered.size()) - 1;
		if (bestInnerIdx != lastBeforeExpand)
			gathered[bestInnerIdx] = gathered[lastBeforeExpand];
		gathered.pop_back();
		// gathered.size() is now n - 2.

		// Add the two binary children; size returns to n.
		gathered.push_back(makeGathered(toExpand->children.left));
		gathered.push_back(makeGathered(toExpand->children.right));
		// gathered.size() == n again; invariant maintained.
	}

	// -----------------------------------------------------------------------
	// Phase 4: commit gathered children to the wide node.
	// -----------------------------------------------------------------------
	// Leaf-type: emit all primitives into orderedPrims via EmitSubtreeLeaves,
	// then write the leaf encoding.
	// Inner-type: recurse into CollapseToWide, then write the child index.
	//
	// wideNodes may reallocate during the recursive CollapseToWide calls.
	// Access wideNodes[nodeIdx] (integer index) after every call; never
	// cache a reference/pointer across a recursive call.
	// -----------------------------------------------------------------------
	for (u_int slot = 0; slot < static_cast<u_int>(gathered.size()); ++slot) {
		const GatheredChild &gc = gathered[slot];

		// Bounding box written before any recursive call (safe: no realloc here).
		wideNodes[nodeIdx].bboxMin[0][slot] = gc.bbox.pMin.x;
		wideNodes[nodeIdx].bboxMin[1][slot] = gc.bbox.pMin.y;
		wideNodes[nodeIdx].bboxMin[2][slot] = gc.bbox.pMin.z;
		wideNodes[nodeIdx].bboxMax[0][slot] = gc.bbox.pMax.x;
		wideNodes[nodeIdx].bboxMax[1][slot] = gc.bbox.pMax.y;
		wideNodes[nodeIdx].bboxMax[2][slot] = gc.bbox.pMax.z;

		if (gc.isLeafType) {
			// Emit primitives from every subNode in this slot.
			u_int primIdx = static_cast<u_int>(oPrims.size());
			for (u_int k = 0; k < gc.nSubNodes; ++k)
				EmitSubtreeLeaves(gc.subNodes[k], leafNodes, oPrims);
			// Leaf encoding: negative childIndex, primCount = number emitted.
			wideNodes[nodeIdx].primCount[slot]  = static_cast<int>(oPrims.size() - primIdx);
			wideNodes[nodeIdx].childIndex[slot] = ~static_cast<int>(primIdx);
			wideNodes[nodeIdx].validChildMask  |= (1 << static_cast<int>(slot));
		} else {
			// Inner: recurse. wideNodes may reallocate; re-index by nodeIdx.
			u_int childWideIdx = CollapseToWide(gc.innerNode, leafNodes, wideNodes, oPrims);
			wideNodes[nodeIdx].childIndex[slot] = static_cast<int>(childWideIdx);
			wideNodes[nodeIdx].primCount[slot]  = 0;
			wideNodes[nodeIdx].validChildMask  |= (1 << static_cast<int>(slot));
		}
	}

	return nodeIdx;
}

// ---------------------------------------------------------------------------
// WorldBound
// ---------------------------------------------------------------------------

BBox MBVHAccel::WorldBound() const
{
	if (nWideNodes == 0)
		return BBox();

	BBox b;
	const MBVHNode &root = wideNodes[0];
	for (int i = 0; i < MBVH_WIDTH; ++i) {
		if (root.childIndex[i] == MBVH_EMPTY_CHILD)
			continue;
		b = Union(b, BBox(
			Point(root.bboxMin[0][i], root.bboxMin[1][i], root.bboxMin[2][i]),
			Point(root.bboxMax[0][i], root.bboxMax[1][i], root.bboxMax[2][i])));
	}
	return b;
}

// ---------------------------------------------------------------------------
// Traversal helpers:
// Combining the slab test with hit filtering inside one noinline function:
// 1. noinline is required so GCC sees the 6 slab loops as a flat (non-nested)
//    sequence. ("loop nest containing two or more consecutive inner loops 
//    cannot be vectorized").
// 2. Returning an int bitmask instead of two float[8] output arrays reduces
//    the argument count from 14 to 12.
// 3. The caller iterates only over *set* bits with __builtin_ctz rather than
//    looping over all 8 slots unconditionally, cutting dispatch iterations
//    from always-8 down to the number of actual child hits.
// ---------------------------------------------------------------------------
// Returns a bitmask: bit i set  <=>  child slot i is non-empty and intersects
// the ray segment [mint, maxt]. The 6 slab loops are each vectorized by
// GCC into 256-bit VMAXPS/VMINPS instructions. The final mask loop is all-float
// so GCC can vectorize it too; node->validChildMask ANDs away any empty slots.
// ---------------------------------------------------------------------------

static __attribute__((noinline, hot)) int
ComputeHitMask(const MBVHNode * __restrict__ node,
               int sx, int sy, int sz,
               float ox, float oy, float oz,
               float idx, float idy, float idz,
               float mint, float maxt)
{
	const float *nearX = sx ? node->bboxMax[0] : node->bboxMin[0];
	const float *farX  = sx ? node->bboxMin[0] : node->bboxMax[0];
	const float *nearY = sy ? node->bboxMax[1] : node->bboxMin[1];
	const float *farY  = sy ? node->bboxMin[1] : node->bboxMax[1];
	const float *nearZ = sz ? node->bboxMax[2] : node->bboxMin[2];
	const float *farZ  = sz ? node->bboxMin[2] : node->bboxMax[2];

	float tminC[MBVH_WIDTH] __attribute__((aligned(MBVH_ALIGN)));
	float tmaxC[MBVH_WIDTH] __attribute__((aligned(MBVH_ALIGN)));

	for (int i = 0; i < MBVH_WIDTH; ++i)
		tminC[i] = (nearX[i] - ox) * idx;
	for (int i = 0; i < MBVH_WIDTH; ++i)
		tmaxC[i] = (farX[i]  - ox) * idx;
	for (int i = 0; i < MBVH_WIDTH; ++i) {
		float lo = (nearY[i] - oy) * idy;
		if (lo > tminC[i]) tminC[i] = lo;
	}
	for (int i = 0; i < MBVH_WIDTH; ++i) {
		float hi = (farY[i] - oy) * idy;
		if (hi < tmaxC[i]) tmaxC[i] = hi;
	}
	for (int i = 0; i < MBVH_WIDTH; ++i) {
		float lo = (nearZ[i] - oz) * idz;
		if (lo > tminC[i]) tminC[i] = lo;
	}
	for (int i = 0; i < MBVH_WIDTH; ++i) {
		float hi = (farZ[i] - oz) * idz;
		if (hi < tmaxC[i]) tmaxC[i] = hi;
	}

	int mask = 0;
	for (int i = 0; i < MBVH_WIDTH; ++i) {
		if (tminC[i] <= tmaxC[i] && tmaxC[i] >= mint && tminC[i] <= maxt)
			mask |= (1 << i);
	}
	// AND with precomputed valid-slot mask to exclude MBVH_EMPTY_CHILD slots
	// without loading/scanning childIndex[8] inside this function.
	return mask & node->validChildMask;
}

// Traversal
bool MBVHAccel::Intersect(const Ray &ray, Intersection *isect) const
{
	if (nWideNodes == 0) return false;

	u_int stack[64];
	int   top    = 0;
	bool  hit    = false;
	stack[top++] = 0u;

	const float ox  = ray.o.x, oy = ray.o.y, oz = ray.o.z;
	const float idx = (ray.d.x != 0.f) ? 1.f / ray.d.x : std::numeric_limits<float>::infinity();
	const float idy = (ray.d.y != 0.f) ? 1.f / ray.d.y : std::numeric_limits<float>::infinity();
	const float idz = (ray.d.z != 0.f) ? 1.f / ray.d.z : std::numeric_limits<float>::infinity();

	// Sign bits precomputed once per ray: 1 => bboxMax is the near plane for
	// that axis (ray travels in the negative direction). This lets us select
	// the near/far arrays with a single pointer branch per axis per node,
	// eliminating all conditional swaps from the slab loops so GCC can emit
	// VMAXPS/VMINPS sequences.
	const int sx = (idx < 0.f) ? 1 : 0;
	const int sy = (idy < 0.f) ? 1 : 0;
	const int sz = (idz < 0.f) ? 1 : 0;

	while (top > 0) {
		const MBVHNode &node = wideNodes[stack[--top]];

		// ComputeHitMask runs the vectorized slab test and returns a
		// bitmask of hit children. Iterating only the set bits with
		// __builtin_ctz (single TZCNT instruction) keeps dispatch iterations
		// at the number of actual hits, instead of always 8.
		int hitMask = ComputeHitMask(&node, sx, sy, sz,
		                             ox, oy, oz, idx, idy, idz,
		                             ray.mint, ray.maxt);
		while (hitMask) {
			const int i  = __builtin_ctz(hitMask);
			hitMask     &= hitMask - 1;   // clear lowest set bit
			const int ci = node.childIndex[i];
			if (ci < 0) {
				const int cnt  = node.primCount[i];
				const int base = ~ci;  // primOffset = ~childIndex (sign-bit encoding)
				for (int k = 0; k < cnt; ++k)
					if (orderedPrims[base + k]->Intersect(ray, isect))
						hit = true;
			} else {
				assert(top < 64);
				stack[top++] = static_cast<u_int>(ci);
			}
		}
	}
	return hit;
}

bool MBVHAccel::IntersectP(const Ray &ray) const
{
	if (nWideNodes == 0) return false;

	u_int stack[64];
	int   top    = 0;
	stack[top++] = 0u;

	const float ox  = ray.o.x, oy = ray.o.y, oz = ray.o.z;
	const float idx = (ray.d.x != 0.f) ? 1.f / ray.d.x : std::numeric_limits<float>::infinity();
	const float idy = (ray.d.y != 0.f) ? 1.f / ray.d.y : std::numeric_limits<float>::infinity();
	const float idz = (ray.d.z != 0.f) ? 1.f / ray.d.z : std::numeric_limits<float>::infinity();

	const int sx = (idx < 0.f) ? 1 : 0;
	const int sy = (idy < 0.f) ? 1 : 0;
	const int sz = (idz < 0.f) ? 1 : 0;

	while (top > 0) {
		const MBVHNode &node = wideNodes[stack[--top]];

		int hitMask = ComputeHitMask(&node, sx, sy, sz,
		                             ox, oy, oz, idx, idy, idz,
		                             ray.mint, ray.maxt);
		while (hitMask) {
			const int i  = __builtin_ctz(hitMask);
			hitMask     &= hitMask - 1;
			const int ci = node.childIndex[i];
			if (ci < 0) {
				const int cnt  = node.primCount[i];
				const int base = ~ci;  // primOffset = ~childIndex (sign-bit encoding)
				for (int k = 0; k < cnt; ++k)
					if (orderedPrims[base + k]->IntersectP(ray))
						return true;
			} else {
				assert(top < 64);
				stack[top++] = static_cast<u_int>(ci);
			}
		}
	}
	return false;
}

// GetPrimitives / CreateAccelerator
void MBVHAccel::GetPrimitives(vector<boost::shared_ptr<Primitive> > &primitives) const
{
	primitives.reserve(nPrims);
	for (u_int i = 0; i < nPrims; ++i)
		primitives.push_back(prims[i]);
}

Aggregate *MBVHAccel::CreateAccelerator(
		const vector<boost::shared_ptr<Primitive> > &prims,
		const ParamSet &ps)
{
	// costsamples: number of SAH candidate splits evaluated per binary split.
	// 0 => centroid-median (fast, lower quality). 8 is a good default.
	int costSamples  = ps.FindOneInt("costsamples", 8);
	// isectCost: ray-triangle intersection cost.
	int isectCost    = ps.FindOneInt("intersectcost", 80);
	// traversalCost: A ratio of 80:1 (matching PBRT) is appropriate;
	// it encourages more splits and a higher quality tree.
	int travCost     = ps.FindOneInt("traversalcost", 1);
	// emptybonus: an all-one-side split provides no spatial separation,
	// so we do not reward it.
	float emptyBonus = ps.FindOneFloat("emptybonus", 0.0f);
	// maxleafprims: maximum primitives allowed in a binary BVH leaf node.
	//
	// This value simultaneously controls two things:
	// 1. Binary BVH stopping criterion: splitting stops when a range covers
	//    <= maxleafprims primitives, so leaves in the binary tree hold at
	//    most this many primitives.
	// 2. Wide BVH leaf slot capacity: CollapseToWide uses
	//    inlineThreshold = MBVH_WIDTH * maxleafprims
	//    to decide when to inline a small binary subtree as a single fat
	//    leaf slot rather than recursing into it. Leaf slots therefore
	//    hold at most MBVH_WIDTH * maxleafprims primitives each.
	int maxLeafPrims = ps.FindOneInt("maxleafprims", 1);
	return new MBVHAccel(prims, costSamples, isectCost, travCost, emptyBonus, maxLeafPrims);
}

static DynamicLoader::RegisterAccelerator<MBVHAccel> r("mbvh");
