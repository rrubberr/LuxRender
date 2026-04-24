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

// bvhaccel.h*

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

	// Range into the partitioned leafNodes array; only valid when
	// isLeaf == true && primitive == nullptr (multi-prim aggregate leaf).
	u_int leafPrimStart, leafPrimEnd;

	BVHAccelTreeNode() : primitive(nullptr), isLeaf(false), sahCost(0.f),
	                     leafPrimStart(0), leafPrimEnd(0) {}
};

// ---------------------------------------------------------------------------
// 8-wide BVH node – Structure-of-Arrays layout so GCC can vectorize.
//
// For each of the (up to) 8 children:
// bboxMin[axis][child]  bboxMax[axis][child]
//
// childIndex[i]:
// >= 0  => inner node at that offset in the flat array
// <  0  => leaf; primOffset = ~childIndex
// MBVH8_EMPTY_CHILD -> slot unused
// ---------------------------------------------------------------------------
static const int MBVH8_WIDTH       = 8;
static const int MBVH8_EMPTY_CHILD = 0x7fffffff;

// Each SoA row (bboxMin[axis] or bboxMax[axis]) is MBVH8_WIDTH floats wide.
// For auto-vectorization the struct must be aligned to that row size so the
// compiler can issue aligned SIMD loads.  This works for any power-of-2 width:
// width=4  -> 16-byte rows -> SSE/NEON alignment.
// width=8  -> 32-byte rows -> AVX2 alignment.
// width=16 -> 64-byte rows -> AVX-512 alignment...?
static const int MBVH8_ALIGN = MBVH8_WIDTH * static_cast<int>(sizeof(float));

struct __attribute__((aligned(MBVH8_ALIGN))) MBVH8Node {
	
	float bboxMin[3][MBVH8_WIDTH]; // SoA bounding boxes: [min][axis][child]
	float bboxMax[3][MBVH8_WIDTH]; // SoA bounding boxes: [max][axis][child]

	// childIndex[i]: inner-node offset, or encoded leaf (negative), or MBVH8_EMPTY_CHILD.
	// For leaf slots: primOffset = ~childIndex[i].
	int childIndex[MBVH8_WIDTH];
	// For leaf slots: number of primitives; 0 for inner nodes / empty slots.
	int primCount[MBVH8_WIDTH];
	// Precomputed bitmask of non-empty slots (bit i set <=> childIndex[i] != MBVH8_EMPTY_CHILD).
	// Used in ComputeHitMask to avoid loading and scanning childIndex[8] just for the empty check.
	int validChildMask;

	MBVH8Node() {
		for (int i = 0; i < MBVH8_WIDTH; ++i) {
			bboxMin[0][i] = bboxMin[1][i] = bboxMin[2][i] =  std::numeric_limits<float>::infinity();
			bboxMax[0][i] = bboxMax[1][i] = bboxMax[2][i] = -std::numeric_limits<float>::infinity();
			childIndex[i] = MBVH8_EMPTY_CHILD;
			primCount[i]  = 0;
		}
		validChildMask = 0;
	}
};

// BVHAccel Declarations
class BVHAccel : public Aggregate {
public:
	// BVHAccel Public Methods
	BVHAccel(const vector<boost::shared_ptr<Primitive> > &p,
		int csamples, int icost, int tcost, float ebonus, int maxleafp = 8);
	virtual ~BVHAccel();
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

	// Collect up to MBVH8_WIDTH children by searching the binary node
	// with the highest surface area until we have 8 slots or only leaves.
	void GatherChildren(BVHAccelTreeNode *node, int maxChildren,
		vector<BVHAccelTreeNode *> &out);

	// Recursively collapse the binary BVH into the flat array.
	// Returns the index of the newly created MBVH8Node.
	u_int CollapseToWide(BVHAccelTreeNode *node,
		const vector<boost::shared_ptr<BVHAccelTreeNode> > &leafNodes,
		vector<MBVH8Node> &wideNodes,
		vector<Primitive *> &orderedPrims);

	// -----------------------------------------------------------------------
	// BVH Private Data.
	// -----------------------------------------------------------------------
	int  costSamples, isectCost, traversalCost, maxLeafPrims;
	float emptyBonus;

	u_int nPrims;
	boost::shared_ptr<Primitive> *prims; // Original prim array (aligned).

	vector<Primitive *> orderedPrims; // Flat ordered primitive pointers for leaves.

	// Flat 8-wide BVH node array.
	MBVH8Node *bvh8;
	u_int      nWideNodes;

	// Object arena for binary tree nodes (freed after construction);
	// a plain vector of raw pointers deleted manually.
	vector<BVHAccelTreeNode *> buildNodes;
};

}//namespace lux
