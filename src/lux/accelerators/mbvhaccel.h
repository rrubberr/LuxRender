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

// mbvhaccel.h*

#include "lux.h"
#include "primitive.h"

namespace lux
{

// ---------------------------------------------------------------------------
// Binary build tree node (used only during construction).
// ---------------------------------------------------------------------------
struct BVHAccelTreeNode {
	BBox bbox;
	union {
		Primitive *primitive; // non-null -> leaf.
		struct {
			BVHAccelTreeNode *left;
			BVHAccelTreeNode *right;
		} children;
	};
	bool isLeaf;

	float sahCost; // SAH collapse cost.

	// Range into the partitioned leafNodes array.
	// Set on ALL node types by BuildBinaryBVH so that CollapseToWide can
	// determine a subtree's primitive count in O(1) without an extra walk:
	//   single-prim leaf  (primitive != nullptr): [i, i+1)
	//   multi-prim leaf   (primitive == nullptr) : [begin, end)
	//   inner node        (isLeaf == false)      : [begin, end) for whole subtree
	u_int leafPrimStart, leafPrimEnd;

	BVHAccelTreeNode() : primitive(nullptr), isLeaf(false), sahCost(0.f),
	                     leafPrimStart(0), leafPrimEnd(0) {}
};

// ---------------------------------------------------------------------------
// Wide BVH node.
//
// For each of the (up to) 16 children:
// bboxMin[axis][child]  bboxMax[axis][child]
//
// childIndex[i]:
// >= 0  => inner node at that offset in the flat array
// <  0  => leaf; primOffset = ~childIndex
// MBVH_EMPTY_CHILD -> slot unused. Max int. value for GCC reasons.
// ---------------------------------------------------------------------------
#if defined(MBVH_TARGET_STR)
    #if (MBVH_TARGET_STR == 30)
        static const int MBVH_WIDTH = 16;
		#pragma message("MBVH_WIDTH = 16")
    #elif (MBVH_TARGET_STR == 20)
        static const int MBVH_WIDTH = 8;
		#pragma message("MBVH_WIDTH = 8")
	#elif (MBVH_TARGET_STR == 10)
		static const int MBVH_WIDTH = 8;
		#pragma message("MBVH_WIDTH = 8")
    #endif
#else
    static const int MBVH_WIDTH = 4;
	#pragma message("MBVH_WIDTH = 4")
#endif

static const int MBVH_EMPTY_CHILD = 0x7fffffff;

// Each SoA row (bboxMin[axis] or bboxMax[axis]) is MBVH_WIDTH floats wide.
// For auto-vectorization the struct must be aligned to that row size so the
// compiler can align SIMD loads. This works for any power-of-2 up to 16.
static const int MBVH_ALIGN = MBVH_WIDTH * static_cast<int>(sizeof(float));

struct __attribute__((aligned(MBVH_ALIGN))) MBVHNode {
	
	float bboxMin[3][MBVH_WIDTH]; // SoA bounding boxes: [min][axis][child]
	float bboxMax[3][MBVH_WIDTH]; // SoA bounding boxes: [max][axis][child]

	// childIndex[i]: inner-node offset, or encoded leaf (negative), or MBVH_EMPTY_CHILD.
	// For leaf slots: primOffset = ~childIndex[i].
	int childIndex[MBVH_WIDTH];
	// For leaf slots: number of primitives; 0 for inner nodes / empty slots.
	int primCount[MBVH_WIDTH];
	// Precomputed bitmask of non-empty slots (bit i set <=> childIndex[i] != MBVH_EMPTY_CHILD).
	// Used in ComputeHitMask to avoid loading and scanning childIndex[8] just for the empty check.
	int validChildMask;

	MBVHNode() {
		for (int i = 0; i < MBVH_WIDTH; ++i) {
			bboxMin[0][i] = bboxMin[1][i] = bboxMin[2][i] =  std::numeric_limits<float>::infinity();
			bboxMax[0][i] = bboxMax[1][i] = bboxMax[2][i] = -std::numeric_limits<float>::infinity();
			childIndex[i] = MBVH_EMPTY_CHILD;
			primCount[i]  = 0;
		}
		validChildMask = 0;
	}
};

// BVHAccel Declarations.
class MBVHAccel : public Aggregate {
public:
	// MBVHAccel Public Methods.
	MBVHAccel(const vector<boost::shared_ptr<Primitive> > &p,
		int csamples, int icost, int tcost, float ebonus, int maxleafp = 8);
	virtual ~MBVHAccel();
	virtual BBox WorldBound() const;
	virtual bool CanIntersect() const { return true; }
	virtual bool Intersect(const Ray &ray, Intersection *isect) const;
	virtual bool IntersectP(const Ray &ray) const;
	virtual Transform GetLocalToWorld(float time) const {
		return Transform();
	}

	virtual void GetPrimitives(vector<boost::shared_ptr<Primitive> > &prims) const;

	static Aggregate *CreateAccelerator(const vector<boost::shared_ptr<Primitive> > &prims, const ParamSet &ps);

private:
	// -----------------------------------------------------------------------
	// Binary BVH construction surface-area heuristic.
	// -----------------------------------------------------------------------
	BVHAccelTreeNode *BuildBinaryBVH(
		vector<boost::shared_ptr<BVHAccelTreeNode> > &leaves,
		u_int begin, u_int end);

	void FindBestSplit(
		vector<boost::shared_ptr<BVHAccelTreeNode> > &list,
		u_int begin, u_int end,
		float *splitValue, u_int *bestAxis);

	// -----------------------------------------------------------------------
	// SAH-driven collapsing pass (Wald 2008).
	// -----------------------------------------------------------------------
	float ComputeCollapseInfo(BVHAccelTreeNode *node, float rootSAInv);

	// Collect up to MBVH_WIDTH children by repeatedly expanding the
	// non-leaf candidate with the highest SAH cost contribution
	// (surface-area*sahCost) until we have maxChildren slots or only
	// leaves remain. Using SA*sahCost rather than raw SA respects primitive
	// density and matches the greedy collapsing intent of Wald 2008, Sec. 4.
	void GatherChildren(BVHAccelTreeNode *node, int maxChildren,
		vector<BVHAccelTreeNode *> &out);

	// Recursively collapse the binary BVH into the flat array.
	// Returns the index of the newly created MBVHNode.
	u_int CollapseToWide(BVHAccelTreeNode *node,
		const vector<boost::shared_ptr<BVHAccelTreeNode> > &leafNodes,
		vector<MBVHNode> &wideNodes,
		vector<Primitive *> &orderedPrims);

	// -----------------------------------------------------------------------
	// BVH Private Data.
	// -----------------------------------------------------------------------
	int  costSamples, isectCost, traversalCost, maxLeafPrims;
	float emptyBonus;

	u_int nPrims;
	boost::shared_ptr<Primitive> *prims; // Original prim array (aligned).

	vector<Primitive *> orderedPrims; // Flat ordered primitive pointers for leaves.

	// Flat wide BVH node array.
	MBVHNode *wideNodes;
	u_int      nWideNodes;

	// Object arena for binary tree nodes (freed after construction);
	// a plain vector of raw pointers deleted manually.
	vector<BVHAccelTreeNode *> buildNodes;
};

}//namespace lux
