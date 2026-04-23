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

// Boundary Volume Hierarchy accelerator — 8-wide multi-branching version.
// Build strategy based on Wald, Benthin & Boulos, "Getting Rid of Packets",
// IEEE/EG Symposium on Interactive Ray Tracing 2008.
// Original scalar BVH code by Ratow; octree split by LuxRender authors.

#include "bvhaccel.h"
#include "paramset.h"
#include "dynload.h"
#include "error.h"

#include <algorithm>
#include <functional>
#include <vector>
#include <cmath>
#include <climits>

using std::bind2nd;
using std::ptr_fun;
using std::vector;
using namespace luxrays;
using namespace lux;

// ─────────────────────────────────────────────────────────────────────────────
// Centroid comparators for partition() in BuildHierarchy — unchanged.
// ─────────────────────────────────────────────────────────────────────────────
static bool bvh_ltf_x(std::shared_ptr<BVHAccelTreeNode> n, float v)
    { return n->bbox.pMax.x + n->bbox.pMin.x < v; }
static bool bvh_ltf_y(std::shared_ptr<BVHAccelTreeNode> n, float v)
    { return n->bbox.pMax.y + n->bbox.pMin.y < v; }
static bool bvh_ltf_z(std::shared_ptr<BVHAccelTreeNode> n, float v)
    { return n->bbox.pMax.z + n->bbox.pMin.z < v; }
static bool (* const bvh_ltf[3])(std::shared_ptr<BVHAccelTreeNode>, float) =
    { bvh_ltf_x, bvh_ltf_y, bvh_ltf_z };

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────
BVHAccel::BVHAccel(const vector<std::shared_ptr<Primitive> > &p,
                   u_int treetype, int csamples, int icost, int tcost,
                   float ebonus)
    : costSamples(csamples), isectCost(icost), traversalCost(tcost),
      emptyBonus(ebonus), nBuildNodes(0),
      nPrims(0), prims(NULL),
      nWBVHNodes(0), wbvhTree(NULL), nLeafPrims(0), leafPrims(NULL)
{
    // Refine all primitives.
    vector<std::shared_ptr<Primitive> > vPrims;
    const PrimitiveRefinementHints refineHints(false);
    for (u_int i = 0; i < p.size(); ++i) {
        if (p[i]->CanIntersect()) vPrims.push_back(p[i]);
        else p[i]->Refine(vPrims, refineHints, p[i]);
    }

    // Always use 8-wide splitting; the collapsing step will improve quality.
    treeType = 8;
    (void)treetype; // user param ignored: we always build 8-wide

    nPrims = vPrims.size();
    prims  = AllocAligned<std::shared_ptr<Primitive> >(nPrims > 0 ? nPrims : 1);
    for (u_int i = 0; i < nPrims; ++i)
        new (&prims[i]) std::shared_ptr<Primitive>(vPrims[i]);

    if (nPrims == 0) {
        wbvhTree  = AllocAligned<WBVHNode>(1);
        wbvhTree[0] = WBVHNode();
        for (int i = 0; i < 8; ++i) wbvhTree[0].children[i] = WBVHNode::EMPTY_CHILD;
        leafPrims = NULL;
        nLeafPrims = 0;
        nWBVHNodes = 1;
        return;
    }

    // Build the initial linked tree (octree split, treeType=8).
    vector<std::shared_ptr<BVHAccelTreeNode> > bvList;
    bvList.reserve(nPrims);
    for (u_int i = 0; i < nPrims; ++i) {
        std::shared_ptr<BVHAccelTreeNode> node(new BVHAccelTreeNode());
        node->bbox = prims[i]->WorldBound();
        node->bbox.Expand(MachineEpsilon::E(node->bbox));
        node->primitive = prims[i].get();
        bvList.push_back(node);
    }

    LOG(LUX_INFO, LUX_NOERROR) << "Building 8-wide BVH, primitives: " << nPrims;
    nBuildNodes = 0;
    std::shared_ptr<BVHAccelTreeNode> root = BuildHierarchy(bvList, 0, bvList.size(), 2);

    LOG(LUX_INFO, LUX_NOERROR) << "Collapsing 8-wide BVH (SAH-based), "
        << "initial nodes: " << nBuildNodes;
    CollapseNode(root);

    // Allocate the WBVHNode array.  nBuildNodes is a safe upper bound on the
    // number of wide nodes because collapsing only reduces node count.
    wbvhTree = AllocAligned<WBVHNode>(nBuildNodes + 1);

    // Build the flat WBVHNode array and collect leaf primitive pointers.
    vector<Primitive *> leafPrimsVec;
    leafPrimsVec.reserve(nPrims);
    nWBVHNodes = BuildWideArray(root, 0, leafPrimsVec);

    // Transfer leaf primitives to a plain array.
    nLeafPrims = static_cast<u_int>(leafPrimsVec.size());
    leafPrims  = new Primitive *[nLeafPrims > 0 ? nLeafPrims : 1];
    for (u_int i = 0; i < nLeafPrims; ++i)
        leafPrims[i] = leafPrimsVec[i];

    // Cache the world bounding box from the root tree node.
    worldBound = root->bbox;

    LOG(LUX_INFO, LUX_NOERROR) << "8-wide BVH complete: "
        << nWBVHNodes << " wide nodes, " << nLeafPrims << " leaf prims";
}

// ─────────────────────────────────────────────────────────────────────────────
// Destructor
// ─────────────────────────────────────────────────────────────────────────────
BVHAccel::~BVHAccel() {
    for (u_int i = 0; i < nPrims; ++i)
        prims[i].~shared_ptr();
    FreeAligned(prims);
    FreeAligned(wbvhTree);
    delete[] leafPrims;
}

// ─────────────────────────────────────────────────────────────────────────────
// BuildHierarchy  —  recursive octree split; unchanged from the original.
// ─────────────────────────────────────────────────────────────────────────────
std::shared_ptr<BVHAccelTreeNode> BVHAccel::BuildHierarchy(
    vector<std::shared_ptr<BVHAccelTreeNode> > &list,
    u_int begin, u_int end, u_int axis)
{
    u_int splitAxis = axis;
    float splitValue;

    nBuildNodes += 1;
    if (end - begin == 1)
        return list[begin]; // single primitive — return leaf directly

    std::shared_ptr<BVHAccelTreeNode> parent(new BVHAccelTreeNode());
    parent->primitive = NULL;
    if (end == begin)
        return parent; // empty subtree

    // Compute up to treeType=8 split points using successive binary splits.
    vector<u_int> splits;
    splits.reserve(treeType + 1);
    splits.push_back(begin);
    splits.push_back(end);

    for (u_int i = 2; i <= treeType; i *= 2) {
        for (u_int j = 0, offset = 0;
             j + offset < i && splits.size() > j + 1; j += 2)
        {
            if (splits[j + 1] - splits[j] < 2) {
                --j; ++offset;
                continue; // fewer than 2 elements — no split needed
            }
            FindBestSplit(list, splits[j], splits[j + 1],
                          &splitValue, &splitAxis);
            vector<std::shared_ptr<BVHAccelTreeNode> >::iterator it =
                partition(list.begin() + splits[j],
                          list.begin() + splits[j + 1],
                          bind2nd(ptr_fun(bvh_ltf[splitAxis]), splitValue));
            u_int mid = distance(list.begin(), it);
            mid = max(splits[j] + 1, min(splits[j + 1] - 1, mid));
            splits.insert(splits.begin() + j + 1, mid);
        }
    }

    // Build child subtrees for each interval.
    std::shared_ptr<BVHAccelTreeNode> child =
        BuildHierarchy(list, splits[0], splits[1], splitAxis);
    parent->leftChild = child;
    parent->bbox = Union(parent->bbox, child->bbox);
    std::shared_ptr<BVHAccelTreeNode> last = child;

    for (u_int i = 1; i < splits.size() - 1; ++i) {
        child = BuildHierarchy(list, splits[i], splits[i + 1], splitAxis);
        last->rightSibling = child;
        parent->bbox = Union(parent->bbox, child->bbox);
        last = child;
    }

    return parent;
}

// ─────────────────────────────────────────────────────────────────────────────
// FindBestSplit  —  unchanged from the original.
// ─────────────────────────────────────────────────────────────────────────────
void BVHAccel::FindBestSplit(vector<std::shared_ptr<BVHAccelTreeNode> > &list,
    u_int begin, u_int end, float *splitValue, u_int *bestAxis)
{
    if (end - begin == 2) {
        *splitValue = (list[begin]->bbox.pMax[0] + list[begin]->bbox.pMin[0] +
                       list[end-1]->bbox.pMax[0] + list[end-1]->bbox.pMin[0]) / 2.f;
        *bestAxis = 0;
        return;
    }

    Point mean2(0, 0, 0), var(0, 0, 0);
    for (u_int i = begin; i < end; ++i)
        mean2 += list[i]->bbox.pMax + list[i]->bbox.pMin;
    mean2 /= static_cast<float>(end - begin);

    for (u_int i = begin; i < end; ++i) {
        Vector v = list[i]->bbox.pMax + list[i]->bbox.pMin - mean2;
        v.x *= v.x; v.y *= v.y; v.z *= v.z;
        var += v;
    }
    if      (var.x > var.y && var.x > var.z) *bestAxis = 0;
    else if (var.y > var.z)                   *bestAxis = 1;
    else                                       *bestAxis = 2;

    if (costSamples > 1) {
        BBox nodeBounds;
        for (u_int i = begin; i < end; ++i)
            nodeBounds = Union(nodeBounds, list[i]->bbox);

        Vector d = nodeBounds.pMax - nodeBounds.pMin;
        float totalSA    = 2.f * (d.x*d.y + d.x*d.z + d.y*d.z);
        float invTotalSA = 1.f / totalSA;
        float increment  = 2.f * d[*bestAxis] / static_cast<float>(costSamples + 1);
        float bestCost   = INFINITY;

        for (float sv  = 2.f * nodeBounds.pMin[*bestAxis] + increment;
                   sv  < 2.f * nodeBounds.pMax[*bestAxis]; sv += increment) {
            int nBelow = 0, nAbove = 0;
            BBox bbBelow, bbAbove;
            for (u_int j = begin; j < end; ++j) {
                if ((list[j]->bbox.pMax[*bestAxis] +
                     list[j]->bbox.pMin[*bestAxis]) < sv) {
                    ++nBelow; bbBelow = Union(bbBelow, list[j]->bbox);
                } else {
                    ++nAbove; bbAbove = Union(bbAbove, list[j]->bbox);
                }
            }
            Vector dB = bbBelow.pMax - bbBelow.pMin;
            Vector dA = bbAbove.pMax - bbAbove.pMin;
            float pB = 2.f*(dB.x*dB.y+dB.x*dB.z+dB.y*dB.z) * invTotalSA;
            float pA = 2.f*(dA.x*dA.y+dA.x*dA.z+dA.y*dA.z) * invTotalSA;
            float eb  = (nAbove == 0 || nBelow == 0) ? emptyBonus : 0.f;
            float cost = traversalCost +
                         isectCost * (1.f - eb) * (pB*nBelow + pA*nAbove);
            if (cost < bestCost) { bestCost = cost; *splitValue = sv; }
        }
    } else {
        *splitValue = mean2[*bestAxis];
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// CollapseNode  —  SAH-based collapsing (Wald et al. 2008, Section 4.2).
//
// For each inner node, we try to pull inner-node children's children directly
// into the current node ("merging a child into its parent").  The paper shows
// this always reduces the expected traversal cost by exactly SA(child), so we
// apply it unconditionally whenever (currentCount - 1 + grandCount) ≤ 8.
// We repeat until no more pulls are possible, then recurse.
//
// Implementation: we work with a std::vector<shared_ptr<BVHAccelTreeNode>>
// for the current node's children, which makes insertions/deletions O(n)
// (n ≤ 8 always) and avoids complicated pointer-surgery on the linked list.
// ─────────────────────────────────────────────────────────────────────────────
void BVHAccel::CollapseNode(std::shared_ptr<BVHAccelTreeNode> node) {
    if (!node || node->primitive)
        return; // null or leaf — nothing to collapse

    // Collect the current children into a vector.
    vector<std::shared_ptr<BVHAccelTreeNode> > children;
    {
        auto c = node->leftChild;
        while (c) { children.push_back(c); c = c->rightSibling; }
    }

    // Repeatedly try to absorb inner-node grandchildren into this node.
    bool changed = true;
    while (changed) {
        changed = false;
        const int n = static_cast<int>(children.size());
        for (int i = 0; i < n && !changed; ++i) {
            auto &ci = children[i];
            // Only inner nodes (primitive == NULL) with children can be absorbed.
            if (ci->primitive || !ci->leftChild) continue;

            // Count ci's children (grandchildren of node).
            int grandCount = 0;
            for (auto gc = ci->leftChild; gc; gc = gc->rightSibling)
                ++grandCount;

            // Absorption replaces ci (cost: -1) with grandCount children.
            // Condition: result fits in 8 slots.
            if (n - 1 + grandCount > 8) continue;

            // Perform absorption: collect grandchildren, splice into children[].
            vector<std::shared_ptr<BVHAccelTreeNode> > grandchildren;
            grandchildren.reserve(grandCount);
            for (auto gc = ci->leftChild; gc; gc = gc->rightSibling)
                grandchildren.push_back(gc);

            // Replace children[i] with the grandchildren in-place.
            children.erase(children.begin() + i);
            children.insert(children.begin() + i,
                            grandchildren.begin(), grandchildren.end());

            changed = true; // restart the scan — new pulls may now be possible
        }
    }

    // Rebuild the rightSibling linked list from the updated children vector.
    node->leftChild.reset();
    if (!children.empty()) {
        node->leftChild = children[0];
        for (int i = 0; i + 1 < static_cast<int>(children.size()); ++i)
            children[i]->rightSibling = children[i + 1];
        children.back()->rightSibling.reset();
    }

    // Recurse into all remaining children.
    for (auto &c : children)
        CollapseNode(c);
}

// ─────────────────────────────────────────────────────────────────────────────
// BuildWideArray  —  converts collapsed linked tree to flat WBVHNode array.
//
// node    : the tree node whose children we are packaging into wbvhTree[nodeIdx]
// nodeIdx : index in wbvhTree[] reserved for this wide node
// returns : the next free index in wbvhTree[] after this entire subtree
//
// Leaf nodes (primitive != NULL) are encoded in children[] as ~leafIndex
// (all negative values) and their Primitive* is appended to leafPrimsVec.
// Unused child slots retain children[i] == EMPTY_CHILD.
// ─────────────────────────────────────────────────────────────────────────────
u_int BVHAccel::BuildWideArray(std::shared_ptr<BVHAccelTreeNode> node,
                               u_int nodeIdx,
                               vector<Primitive *> &leafPrimsVec)
{
    WBVHNode &wn = wbvhTree[nodeIdx];

    // Initialise all 8 slots as empty.  Padding bbox values are +INFINITY so
    // the slab test always fails: for positive invDir both t1 and t2 are +INF,
    // driving tEntry to +INF > tExit; for negative invDir tExit collapses to
    // -INF < tEntry.  Either way the slot is never reported as a hit.
    const float kInf = std::numeric_limits<float>::infinity();
    for (int i = 0; i < 8; ++i) {
        wn.children[i] = WBVHNode::EMPTY_CHILD;
        for (int ax = 0; ax < 3; ++ax) {
            wn.bboxMin[ax][i] = kInf;
            wn.bboxMax[ax][i] = kInf;
        }
    }

    // Handle the degenerate case where the root itself is a primitive leaf
    // (only occurs when nPrims == 1).
    if (node->primitive) {
        for (int ax = 0; ax < 3; ++ax) {
            wn.bboxMin[ax][0] = node->bbox.pMin[ax];
            wn.bboxMax[ax][0] = node->bbox.pMax[ax];
        }
        wn.children[0] = ~static_cast<int>(leafPrimsVec.size());
        leafPrimsVec.push_back(node->primitive);
        return nodeIdx + 1;
    }

    // General case: inner node with up to 8 children.
    u_int nextFree = nodeIdx + 1;
    int ci = 0;
    for (auto child = node->leftChild; child && ci < 8;
         child = child->rightSibling, ++ci)
    {
        // Write the child's bounding box into this wide node's SoA arrays.
        for (int ax = 0; ax < 3; ++ax) {
            wn.bboxMin[ax][ci] = child->bbox.pMin[ax];
            wn.bboxMax[ax][ci] = child->bbox.pMax[ax];
        }

        if (child->primitive) {
            // Leaf: encode as bitwise-complement of the leaf prim index.
            wn.children[ci] = ~static_cast<int>(leafPrimsVec.size());
            leafPrimsVec.push_back(child->primitive);
        } else {
            // Inner node: assign it the next free slot and recurse.
            const u_int childIdx = nextFree;
            wn.children[ci]      = static_cast<int>(childIdx);
            nextFree = BuildWideArray(child, childIdx, leafPrimsVec);
        }
    }

    return nextFree;
}

// ─────────────────────────────────────────────────────────────────────────────
// WorldBound
// ─────────────────────────────────────────────────────────────────────────────
BBox BVHAccel::WorldBound() const {
    return worldBound;
}

// ─────────────────────────────────────────────────────────────────────────────
// Intersect  —  single-ray 8-wide BVH traversal (Wald et al. 2008, Section 5).
//
// Design for compiler auto-vectorisation:
//   - invDir[] and org[] are plain float[3] arrays (no struct members in loops)
//   - tEntry[8] / tExit[8] are fixed-size float arrays on the stack
//   - The slab test is three nested loops: outer=axis (3), inner=child (8)
//     with no dependencies across iterations and no branches in the inner loop
//   - fminf/fmaxf expand to conditional-move sequences on x86, avoiding
//     the branch mispredictions that killed the original scalar traversal
//
// The compiler with -O2 -march=native auto-vectorises the inner 8-element
// loops as 256-bit (AVX2) or 128-bit (SSE) operations depending on the target.
// No explicit intrinsics are required or used.
// ─────────────────────────────────────────────────────────────────────────────
bool BVHAccel::Intersect(const Ray &ray, Intersection *isect) const {
    bool hit = false;

    // Broadcast per-ray constants once.  r_maxt is NOT pre-captured here
    // because ray.maxt is modified by leaf hits — see the note in the loop.
    const __m256 r_mint = _mm256_set1_ps(ray.mint);
    const __m256 r_org[3] = {
        _mm256_set1_ps(ray.o.x),
        _mm256_set1_ps(ray.o.y),
        _mm256_set1_ps(ray.o.z)
    };
    const __m256 r_invDir[3] = {
        _mm256_set1_ps(1.f / ray.d.x),
        _mm256_set1_ps(1.f / ray.d.y),
        _mm256_set1_ps(1.f / ray.d.z)
    };

    int stackTop = 0;
    int stack[64];
    stack[0] = 0; // root node

    while (stackTop >= 0) {
        const int nodeIdx = stack[stackTop--];
        const WBVHNode &node = wbvhTree[nodeIdx];

        // Re-read ray.maxt every node: MeshBaryTriangle::Intersect writes
        // ray.maxt = t on every hit to tighten the ray.  Capturing it once
        // before the loop meant subsequent node tests never saw the tightened
        // bound, disabling all BVH traversal pruning.
        __m256 tEntry = r_mint;
        __m256 tExit  = _mm256_set1_ps(ray.maxt);

        // ── SIMD Slab test for all 8 children simultaneously ──────────────
        for (int ax = 0; ax < 3; ++ax) {
            // Load 8-wide bounding box bounds for the current axis
            __m256 boundsMin = _mm256_load_ps(node.bboxMin[ax]);
            __m256 boundsMax = _mm256_load_ps(node.bboxMax[ax]);

            // t1 = (boundsMin - org) * invDir
            __m256 t1 = _mm256_mul_ps(_mm256_sub_ps(boundsMin, r_org[ax]), r_invDir[ax]);
            // t2 = (boundsMax - org) * invDir
            __m256 t2 = _mm256_mul_ps(_mm256_sub_ps(boundsMax, r_org[ax]), r_invDir[ax]);

            __m256 tMin = _mm256_min_ps(t1, t2);
            __m256 tMax = _mm256_max_ps(t1, t2);

            tEntry = _mm256_max_ps(tEntry, tMin);
            tExit  = _mm256_min_ps(tExit, tMax);
        }

        // Compare tEntry <= tExit. Returns a mask of 0xFFFFFFFF for true, 0x0 for false.
        __m256 cmpMask = _mm256_cmp_ps(tEntry, tExit, _CMP_LE_OQ);
        
        // Extract the highest bit of each 32-bit float into an 8-bit integer mask
        int hitMask = _mm256_movemask_ps(cmpMask);

        // ── Dispatch hit children via bit-scan ────────────────────────────
        // Process hits right-to-left to keep the original stack popping order.
        while (hitMask != 0) {
            // Find the index of the most significant set bit (GCC/Clang builtin)
            int i = 31 - __builtin_clz(hitMask);
            hitMask ^= (1 << i); // Clear the processed bit

            const int child = node.children[i];
            
            // Empty slots will typically fail the intersection due to degenerate 
            // bounds, but if one sneaks through, catch it here.
            if (child == WBVHNode::EMPTY_CHILD) continue; 

            if (child >= 0) {
                // Inner node: push onto stack.
                stack[++stackTop] = child;
            } else {
                // Leaf: test the primitive.
                if (leafPrims[~child]->Intersect(ray, isect)) {
                    hit = true;
                }
            }
        }
    }

    return hit;
}

// ─────────────────────────────────────────────────────────────────────────────
// IntersectP  —  shadow-ray version with early exit on first hit.
// Identical structure to Intersect; exits as soon as any primitive is hit.
// ─────────────────────────────────────────────────────────────────────────────
bool BVHAccel::IntersectP(const Ray &ray) const {
    const __m256 r_mint = _mm256_set1_ps(ray.mint);
    const __m256 r_org[3] = {
        _mm256_set1_ps(ray.o.x),
        _mm256_set1_ps(ray.o.y),
        _mm256_set1_ps(ray.o.z)
    };
    const __m256 r_invDir[3] = {
        _mm256_set1_ps(1.f / ray.d.x),
        _mm256_set1_ps(1.f / ray.d.y),
        _mm256_set1_ps(1.f / ray.d.z)
    };

    int stackTop = 0;
    int stack[64];
    stack[0] = 0;

    while (stackTop >= 0) {
        const int nodeIdx = stack[stackTop--];
        const WBVHNode &node = wbvhTree[nodeIdx];

        __m256 tEntry = r_mint;
        __m256 tExit  = _mm256_set1_ps(ray.maxt); // ray.maxt unchanged in IntersectP, but kept symmetric

        for (int ax = 0; ax < 3; ++ax) {
            __m256 boundsMin = _mm256_load_ps(node.bboxMin[ax]);
            __m256 boundsMax = _mm256_load_ps(node.bboxMax[ax]);

            __m256 t1 = _mm256_mul_ps(_mm256_sub_ps(boundsMin, r_org[ax]), r_invDir[ax]);
            __m256 t2 = _mm256_mul_ps(_mm256_sub_ps(boundsMax, r_org[ax]), r_invDir[ax]);

            __m256 tMin = _mm256_min_ps(t1, t2);
            __m256 tMax = _mm256_max_ps(t1, t2);

            tEntry = _mm256_max_ps(tEntry, tMin);
            tExit  = _mm256_min_ps(tExit, tMax);
        }

        __m256 cmpMask = _mm256_cmp_ps(tEntry, tExit, _CMP_LE_OQ);
        int hitMask = _mm256_movemask_ps(cmpMask);

        while (hitMask != 0) {
            int i = 31 - __builtin_clz(hitMask);
            hitMask ^= (1 << i); 

            const int child = node.children[i];
            if (child == WBVHNode::EMPTY_CHILD) continue;

            if (child >= 0) {
                stack[++stackTop] = child;
            } else {
                if (leafPrims[~child]->IntersectP(ray)) {
                    return true; // Shadow ray — early exit on first hit
                }
            }
        }
    }

    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// GetPrimitives / CreateAccelerator
// ─────────────────────────────────────────────────────────────────────────────
void BVHAccel::GetPrimitives(
    vector<std::shared_ptr<Primitive> > &primitives) const
{
    primitives.reserve(primitives.size() + nPrims);
    for (u_int i = 0; i < nPrims; ++i)
        primitives.push_back(prims[i]);
}

Aggregate *BVHAccel::CreateAccelerator(
    const vector<std::shared_ptr<Primitive> > &prims, const ParamSet &ps)
{
    // treetype is accepted for backward compatibility but internally always 8.
    int treeType    = ps.FindOneInt("treetype", 8);
    int costSamples = ps.FindOneInt("costsamples", 0);
    int isectCost   = ps.FindOneInt("intersectcost", 80);
    int travCost    = ps.FindOneInt("traversalcost", 10);
    float emptyBonus = ps.FindOneFloat("emptybonus", 0.5f);
    return new BVHAccel(prims, treeType, costSamples, isectCost,
                        travCost, emptyBonus);
}

static DynamicLoader::RegisterAccelerator<BVHAccel> r("bvh");
