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

// bvhaccel.h

#ifndef LUX_BVHACCEL_H
#define LUX_BVHACCEL_H

#include "lux.h"
#include "primitive.h"

#include <climits>

#include <immintrin.h>
#include "memory.h"

namespace lux
{

// ─────────────────────────────────────────────────────────────────────────────
// BVHAccelTreeNode  —  temporary linked-list tree used during construction.
// Unchanged from the original; BuildHierarchy populates it.
// ─────────────────────────────────────────────────────────────────────────────
struct BVHAccelTreeNode {
    BBox bbox;
    Primitive *primitive;
    std::shared_ptr<BVHAccelTreeNode> leftChild;
    std::shared_ptr<BVHAccelTreeNode> rightSibling;
};

// ─────────────────────────────────────────────────────────────────────────────
// WBVHNode  —  8-wide BVH node with SoA bounding-box layout.
// Inherits from Aligned32 to guarantee safe AVX2 loads.
// ─────────────────────────────────────────────────────────────────────────────
struct WBVHNode : public luxrays::Aligned32 {
    // SoA bbox: [axis 0/1/2][child 0..7]
    float bboxMin[3][8];
    float bboxMax[3][8];

    static const int EMPTY_CHILD = 0x7fffffff;
    int children[8];
};

// ─────────────────────────────────────────────────────────────────────────────
// BVHAccel  —  8-wide multi-branching BVH (Wald, Benthin & Boulos 2008).
//
// Build pipeline (Section 4 of the paper):
//   1. BuildHierarchy (treeType=8) — standard recursive octree split using
//      FindBestSplit's variance/SAH heuristic.  Produces a linked tree with
//      up to 8 children per inner node.
//   2. CollapseNode — top-down SAH-based collapsing.  Pulls inner-node
//      grandchildren into the parent whenever (count - 1 + grandCount) ≤ 8.
//      The paper proves this always reduces traversal cost (gain = SA(child)),
//      so we perform it unconditionally until no further pulls are possible.
//   3. BuildWideArray — converts the collapsed tree into a flat WBVHNode
//      array with SoA float[8] bbox layout and index-encoded child links.
//
// Traversal (Section 5):
//   Each inner-node test runs three fixed 8-element loops (one per axis) that
//   the compiler auto-vectorises.  Child links are then processed scalarly.
//   No AVX intrinsics are used; vectorisation is entirely compiler-driven.
// ─────────────────────────────────────────────────────────────────────────────
class BVHAccel : public Aggregate {
public:
    BVHAccel(const vector<std::shared_ptr<Primitive> > &p, u_int treetype,
             int csamples, int icost, int tcost, float ebonus);
    virtual ~BVHAccel();

    virtual BBox WorldBound() const;
    virtual bool CanIntersect() const { return true; }
    virtual bool Intersect(const Ray &ray, Intersection *isect) const;
    virtual bool IntersectP(const Ray &ray) const;
    virtual Transform GetLocalToWorld(float /*time*/) const { return Transform(); }
    virtual void GetPrimitives(vector<std::shared_ptr<Primitive> > &prims) const;

    static Aggregate *CreateAccelerator(
        const vector<std::shared_ptr<Primitive> > &prims, const ParamSet &ps);

private:
    // ── build helpers (unchanged from original) ───────────────────────────
    std::shared_ptr<BVHAccelTreeNode> BuildHierarchy(
        vector<std::shared_ptr<BVHAccelTreeNode> > &list,
        u_int begin, u_int end, u_int axis);
    void FindBestSplit(
        vector<std::shared_ptr<BVHAccelTreeNode> > &list,
        u_int begin, u_int end, float *splitValue, u_int *bestAxis);

    // ── SAH-based collapsing (Wald et al. 2008, Section 4.2) ─────────────
    // Pulls inner-node grandchildren into the parent when profitable and
    // within the 8-child capacity, top-down, then recurses into children.
    void CollapseNode(std::shared_ptr<BVHAccelTreeNode> node);

    // ── WBVHNode array construction ───────────────────────────────────────
    // Converts the collapsed linked tree to a flat WBVHNode array.
    // Returns the next free node index after this subtree.
    u_int BuildWideArray(std::shared_ptr<BVHAccelTreeNode> node,
                         u_int nodeIdx,
                         vector<Primitive *> &leafPrimsVec);

    // ── build-time state ──────────────────────────────────────────────────
    u_int treeType;
    int   costSamples, isectCost, traversalCost;
    float emptyBonus;
    u_int nBuildNodes;  // running counter used by BuildHierarchy

    // ── primitives (kept for GetPrimitives) ───────────────────────────────
    u_int nPrims;
    std::shared_ptr<Primitive> *prims;

    // ── wide BVH runtime data ─────────────────────────────────────────────
    u_int     nWBVHNodes;
    WBVHNode *wbvhTree;   // flat SoA node array
    u_int     nLeafPrims;
    Primitive **leafPrims; // leaf primitive pointers, indexed by ~children[i]
    BBox      worldBound;
};

} // namespace lux
#endif // LUX_BVHACCEL_H