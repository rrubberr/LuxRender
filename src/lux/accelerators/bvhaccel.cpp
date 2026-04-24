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
// BVH accelerator
// ---------------------------------------------------------------------------
// Construction pipeline (Wald 2008, "Stackless Multi-BVH Traversal"):
//   1. Build a standard SAH binary BVH over the primitives.
//   2. Annotate each binary node with its SAH collapse cost.
//   3. Collapse the binary tree into a wide BVH.
//   4. Lay out the resulting wide nodes in a depth-first flat array.
// The MBVH8Node bounding boxes are stored in a structure-of-arrays
// so that GCC can vectorize the 8-wide intersection test without intrinsics.
// ---------------------------------------------------------------------------

// bvhaccel.cpp*

#include "bvhaccel.h"
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

BVHAccel::BVHAccel(const vector<boost::shared_ptr<Primitive> > &p,
		int csamples, int icost, int tcost, float ebonus, int maxleafp)
	: costSamples(csamples), isectCost(icost), traversalCost(tcost),
	  maxLeafPrims(maxleafp < 1 ? 1 : maxleafp),
	  emptyBonus(ebonus), nPrims(0), prims(nullptr), bvh8(nullptr), nWideNodes(0)
{
	// ------------------------------------------------------------------
	// Collect intersectable primitives.
	// ------------------------------------------------------------------
	vector<boost::shared_ptr<Primitive> > vPrims;
	const PrimitiveRefinementHints refineHints(false);
	for (size_t i = 0; i < p.size(); ++i) {
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

	LOG(LUX_INFO, LUX_NOERROR) << "Building binary SAH BVH, primitives: " << nPrims
		<< ", max leaf prims: " << maxLeafPrims;

	// Build binary BVH.
	BVHAccelTreeNode *root = BuildBinaryBVH(leafNodes, 0, nPrims);

	// Annotate nodes with SAH cost.
	float rootSA    = BBoxSurfaceArea(root->bbox);
	float rootSAInv = (rootSA > 0.f) ? 1.f / rootSA : 0.f;
	ComputeCollapseInfo(root, rootSAInv);

	// Collapse binary tree.
	LOG(LUX_INFO, LUX_NOERROR) << "Collapsing binary BVH to " << MBVH8_WIDTH << "-wide BVH";

	vector<MBVH8Node> wideNodesTmp;
	wideNodesTmp.reserve(nPrims);
	CollapseToWide(root, leafNodes, wideNodesTmp, orderedPrims);

	nWideNodes = static_cast<u_int>(wideNodesTmp.size());
	bvh8 = AllocAligned<MBVH8Node>(nWideNodes, MBVH8_ALIGN);
	for (u_int i = 0; i < nWideNodes; ++i)
		new (&bvh8[i]) MBVH8Node(wideNodesTmp[i]);

	// Compute and log quality statistics
	// This writes slot counts and outputs fill histogram.
	u_int totalFilled = 0, totalLeafSlots = 0, totalInnerSlots = 0;
	u_int fillHist[MBVH8_WIDTH + 1] = {}; // fillHist[k] = #nodes with k filled slots.
	for (u_int i = 0; i < nWideNodes; ++i) {
		int nodeFill = 0;
		for (int s = 0; s < MBVH8_WIDTH; ++s) {
			int ci = wideNodesTmp[i].childIndex[s];
			if (ci == MBVH8_EMPTY_CHILD) continue;
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
			for (int s = 0; s < MBVH8_WIDTH; ++s) {
				int ci = wideNodesTmp[ni].childIndex[s];
				if (ci == MBVH8_EMPTY_CHILD) continue;
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
		<< "Finished " << MBVH8_WIDTH << "-wide BVH:"
		<< " nodes: "        << nWideNodes
		<< ", inner slots: " << totalInnerSlots
		<< ", leaf slots: "  << totalLeafSlots
		<< ", avg fill: "    << std::fixed << std::setprecision(2) << avgFill
		<< "/" << MBVH8_WIDTH
		<< " (" << std::fixed << std::setprecision(1)
		<< (100.f * avgFill / MBVH8_WIDTH) << "%)"
		<< ", depth max/avg: " << maxDepth
		<< "/" << std::fixed << std::setprecision(1) << avgLeafDepth;

	// Fill histogram: only print non-zero buckets.
	{
		std::ostringstream hist;
		hist << "Fill histogram (slots:nodes) –";
		for (int k = 1; k <= MBVH8_WIDTH; ++k) {
			if (fillHist[k] > 0)
				hist << " " << k << ":" << fillHist[k];
		}
		LOG(LUX_INFO, LUX_NOERROR) << hist.str();
	}
}

BVHAccel::~BVHAccel()
{
	for (u_int i = 0; i < nPrims; ++i)
		prims[i].~shared_ptr();
	FreeAligned(prims);
	if (bvh8) {
		for (u_int i = 0; i < nWideNodes; ++i)
			bvh8[i].~MBVH8Node();
		FreeAligned(bvh8);
	}
}

// Binary SAH BVH construction.
BVHAccelTreeNode *BVHAccel::BuildBinaryBVH(
		vector<boost::shared_ptr<BVHAccelTreeNode> > &leaves,
		u_int begin, u_int end)
{
	assert(begin < end);

	// Stop recursing once the range fits in a single wide leaf slot.
	if (end - begin <= static_cast<u_int>(maxLeafPrims)) {
		if (end - begin == 1)
			return leaves[begin].get(); // Reuse the already-created single-prim leaf.

		// Primitive aggregate leaf: covers multiple primitive leaves[begin..end).
		// primitive == nullptr distinguishes it from a single-prim leaf.
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

	return node;
}

void BVHAccel::FindBestSplit(
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
		// (for split planes). Realistically, costSamples should ALWAYS be> 1.
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
			if (cext[axis] <= 0.f) continue; // all centroids coincide on this axis
			
			// Sample candidate planes evenly within the centroid range.
			// splitValue is in "2x centroid" space to match the partition
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
// i.e., sahCost = C(n) / SA(n). This makes the recursive formula:
// C_inner(n) / SA(n) = traversalCost/SA(n) + SA(left)*sahCost(left)/SA(n)
//                                          + SA(right)*sahCost(right)/SA(n)
// ---------------------------------------------------------------------------

float BVHAccel::ComputeCollapseInfo(BVHAccelTreeNode *node, float rootSAInv)
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
// Starting from a node, repeatedly replace the non-leaf-candidate with the
// highest surface area, by its two children, until we have filled all slots
// or all candidates are leaves.
// ---------------------------------------------------------------------------

void BVHAccel::GatherChildren(BVHAccelTreeNode *node, int maxChildren,
		vector<BVHAccelTreeNode *> &out)
{
	if (node->isLeaf) {
		out.push_back(node);
		return;
	}

	// Seed with the two direct children of this (non-leaf) binary node
	out.push_back(node->children.left);
	out.push_back(node->children.right);

	while (static_cast<int>(out.size()) < maxChildren) {
		// Find the non-leaf candidate with the largest surface area
		int   bestIdx = -1;
		float bestSA  = -1.f;
		for (int i = 0; i < static_cast<int>(out.size()); ++i) {
			if (!out[i]->isLeaf) {
				float sa = BBoxSurfaceArea(out[i]->bbox);
				if (sa > bestSA) {
					bestSA  = sa;
					bestIdx = i;
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

// Collapse the binary tree into a flat array.
u_int BVHAccel::CollapseToWide(BVHAccelTreeNode *node,
		const vector<boost::shared_ptr<BVHAccelTreeNode> > &leafNodes,
		vector<MBVH8Node> &wideNodes,
		vector<Primitive *> &oPrims)
{
	// Allocate a slot for this wide node up front.
	u_int nodeIdx = static_cast<u_int>(wideNodes.size());
	wideNodes.emplace_back();

	// Gather up to MBVH8_WIDTH children via the SAH strategy.
	vector<BVHAccelTreeNode *> children;
	children.reserve(MBVH8_WIDTH);
	GatherChildren(node, MBVH8_WIDTH, children);

	for (size_t slot = 0; slot < children.size(); ++slot) {
		BVHAccelTreeNode *ch = children[slot];

		// Store bounding box in SoA layout for vectorizable traversal.
		wideNodes[nodeIdx].bboxMin[0][slot] = ch->bbox.pMin.x;
		wideNodes[nodeIdx].bboxMin[1][slot] = ch->bbox.pMin.y;
		wideNodes[nodeIdx].bboxMin[2][slot] = ch->bbox.pMin.z;
		wideNodes[nodeIdx].bboxMax[0][slot] = ch->bbox.pMax.x;
		wideNodes[nodeIdx].bboxMax[1][slot] = ch->bbox.pMax.y;
		wideNodes[nodeIdx].bboxMax[2][slot] = ch->bbox.pMax.z;

		// Emit primitives from one binary leaf node into oPrims.
		auto emitBinaryLeaf = [&](BVHAccelTreeNode *leaf) {
			if (leaf->primitive != nullptr) {
				oPrims.push_back(leaf->primitive);
			} else {
				for (u_int pi = leaf->leafPrimStart; pi < leaf->leafPrimEnd; ++pi)
					oPrims.push_back(leafNodes[pi]->primitive);
			}
		};

		if (ch->isLeaf) {
			// Leaf: record primitive(s) in ordered list; encode slot as leaf.
			u_int primIdx = static_cast<u_int>(oPrims.size());
			emitBinaryLeaf(ch);
			wideNodes[nodeIdx].primCount[slot]  = static_cast<int>(oPrims.size() - primIdx);
			// Negative childIndex signals a leaf; primOffset = ~childIndex (sign-bit trick).
			wideNodes[nodeIdx].childIndex[slot] = ~static_cast<int>(primIdx);
			wideNodes[nodeIdx].validChildMask  |= (1 << static_cast<int>(slot));
		} else if (ch->children.left->isLeaf && ch->children.right->isLeaf) {
			// ---------------------------------------------------------------------------
			// Leaf-parent: this binary inner node's children are both leaves.
			// GatherChildren fills up to 8 items and stops, so a tree that is
			// >=4 binary levels above its leaf nodes will produce exactly 8
			// leaf-parents as gathered child nodes. Each would become a fill=2
			// wide node on its own. Avoid this by inlining both child-nodes'
			// primitives directly into this slot: the slot bbox (ch->bbox =
			// Union of the two leaf bboxes, already written above) covers all
			// primitives.
			// ---------------------------------------------------------------------------
			u_int primIdx = static_cast<u_int>(oPrims.size());
			emitBinaryLeaf(ch->children.left);
			emitBinaryLeaf(ch->children.right);
			wideNodes[nodeIdx].primCount[slot]  = static_cast<int>(oPrims.size() - primIdx);
			wideNodes[nodeIdx].childIndex[slot] = ~static_cast<int>(primIdx);
			wideNodes[nodeIdx].validChildMask  |= (1 << static_cast<int>(slot));
		} else {
			// General inner node: recurse and record child wide-node index.
			// We must index through wideNodes[nodeIdx] after the recursive
			// call because the vector may have been reallocated.
			u_int childWideIdx = CollapseToWide(ch, leafNodes, wideNodes, oPrims);
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

BBox BVHAccel::WorldBound() const
{
	if (nWideNodes == 0)
		return BBox();

	BBox b;
	const MBVH8Node &root = bvh8[0];
	for (int i = 0; i < MBVH8_WIDTH; ++i) {
		if (root.childIndex[i] == MBVH8_EMPTY_CHILD)
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
ComputeHitMask(const MBVH8Node * __restrict__ node,
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

	float tminC[MBVH8_WIDTH] __attribute__((aligned(MBVH8_ALIGN)));
	float tmaxC[MBVH8_WIDTH] __attribute__((aligned(MBVH8_ALIGN)));

	for (int i = 0; i < MBVH8_WIDTH; ++i)
		tminC[i] = (nearX[i] - ox) * idx;
	for (int i = 0; i < MBVH8_WIDTH; ++i)
		tmaxC[i] = (farX[i]  - ox) * idx;
	for (int i = 0; i < MBVH8_WIDTH; ++i) {
		float lo = (nearY[i] - oy) * idy;
		if (lo > tminC[i]) tminC[i] = lo;
	}
	for (int i = 0; i < MBVH8_WIDTH; ++i) {
		float hi = (farY[i] - oy) * idy;
		if (hi < tmaxC[i]) tmaxC[i] = hi;
	}
	for (int i = 0; i < MBVH8_WIDTH; ++i) {
		float lo = (nearZ[i] - oz) * idz;
		if (lo > tminC[i]) tminC[i] = lo;
	}
	for (int i = 0; i < MBVH8_WIDTH; ++i) {
		float hi = (farZ[i] - oz) * idz;
		if (hi < tmaxC[i]) tmaxC[i] = hi;
	}

	int mask = 0;
	for (int i = 0; i < MBVH8_WIDTH; ++i) {
		if (tminC[i] <= tmaxC[i] && tmaxC[i] >= mint && tminC[i] <= maxt)
			mask |= (1 << i);
	}
	// AND with precomputed valid-slot mask to exclude MBVH8_EMPTY_CHILD slots
	// without loading/scanning childIndex[8] inside this function.
	return mask & node->validChildMask;
}

// Traversal
bool BVHAccel::Intersect(const Ray &ray, Intersection *isect) const
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
		const MBVH8Node &node = bvh8[stack[--top]];

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

bool BVHAccel::IntersectP(const Ray &ray) const
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
		const MBVH8Node &node = bvh8[stack[--top]];

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
void BVHAccel::GetPrimitives(vector<boost::shared_ptr<Primitive> > &primitives) const
{
	primitives.reserve(nPrims);
	for (u_int i = 0; i < nPrims; ++i)
		primitives.push_back(prims[i]);
}

Aggregate *BVHAccel::CreateAccelerator(
		const vector<boost::shared_ptr<Primitive> > &prims,
		const ParamSet &ps)
{
	// costsamples: number of SAH candidate splits evaluated per binary split.
	// 0 => centroid-median (fast, lower quality). 8 is a good default.
	int costSamples  = ps.FindOneInt("costsamples", 8);
	// isectCost: ray-triangle intersection cost.
	int isectCost    = ps.FindOneInt("intersectcost", 80);
	// traversalCost: A ratio of 80:1 (matching PBRT) is appropriate;
	//it encourages more splits and a higher quality tree.
	int travCost     = ps.FindOneInt("traversalcost", 1);
	// emptybonus: for a BVH an all-one-side split provides no spatial separation,
	// so we do not reward it.
	float emptyBonus = ps.FindOneFloat("emptybonus", 0.0f);
	// maxleafprims: max primitives per wide leaf slot. Higher values reduce
	// node count and improve fill at the cost of slightly coarser culling.
	// 8 is a good default.
	int maxLeafPrims = ps.FindOneInt("maxleafprims", 8);
	return new BVHAccel(prims, costSamples, isectCost, travCost, emptyBonus, maxLeafPrims);
}

static DynamicLoader::RegisterAccelerator<BVHAccel> r("bvh");
