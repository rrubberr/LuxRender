#pragma once
#include "lux.h"
#include "primitive.h"

#include <immintrin.h>
#include <xmmintrin.h>
#include "simd.h"
#include "paramset.h"
#include "shapes/mesh.h"

#define OBJECT_SPLIT_BINS 8

using namespace lux;

template <size_t I>
struct nray
{
	vfloat<I> ox, oy, oz;
	vfloat<I> dx, dy, dz;
	mutable vfloat<I> mint, maxt;

	nray(const lux::Ray &ray)
	{
		ox = ray.o.x;
		oy = ray.o.y;
		oz = ray.o.z;
		dx = ray.d.x;
		dy = ray.d.y;
		dz = ray.d.z;
		mint = ray.mint;
		maxt = ray.maxt;
	};
};

template <size_t I>
struct nprim : public lux::Aggregate
{
	boost::shared_ptr<Primitive> primitives[I];

	nprim(std::vector<boost::shared_ptr<Primitive>> p)
	{
		for(size_t i = 0; i < I; i++)
			primitives[i] = p[i];
	};

	virtual ~nprim()
	{

	};

	virtual BBox WorldBound() const
	{
		BBox b = Union(primitives[0]->WorldBound(),primitives[1]->WorldBound());
		for(size_t i = 2; i < I; i++)
			b = Union(b,primitives[i]->WorldBound());
		return b;
	};

	virtual bool Intersect(const Ray &ray, Intersection *isect) const
	{
		bool hit = false;
		for(size_t i = 0; i < I; i++)
			hit |= primitives[i]->Intersect(ray, isect);
		return hit;
	};

	virtual bool IntersectP(const Ray &ray) const
	{
		for(size_t i = 0; i < I; i++)
			if(primitives[i]->IntersectP(ray))
				return true;
		return false;
	};

	virtual Transform GetLocalToWorld(float time) const {
		return Transform();
	}
	virtual void GetPrimitives(vector<boost::shared_ptr<Primitive> > &prims) const
	{
		prims.reserve(prims.size() + I);
		for(size_t i = 0; i < I; i++)
			prims.push_back(primitives[i]);
	}
	virtual bool Intersect(const nray<I> &ray4, const Ray &ray, Intersection *isect) const
	{
		const bool hit = Intersect(ray, isect);
		if(!hit)
			return false;
		ray4.maxt = ray.maxt;
		return true;
	}
};

static inline vfloat<4> reciprocal(const vfloat<4> &x)
{
	vfloat<4> y; //12 digits of precision
	y.as<__m128>() = _mm_rcp_ps(x.as<__m128>());
	return y * (2.f - (x * y));
}

static inline vfloat<8> reciprocal(const vfloat<8> &x)
{
	vfloat<8> y; //12 digits of precision
	y.as<__m256>() = _mm256_rcp_ps(x.as<__m256>());
	return y * (2.f - (x * y));
};

static inline vfloat<16> reciprocal(const vfloat<16> &x)
{
	vfloat<16> y; //14 digits of precision (only option)
	y.as<__m512>() = _mm512_rcp14_ps(x.as<__m512>());
	return y * (2.f - (x * y));
};

inline bool check_test(uint8_t test, size_t index)
{
	return test & (0x1u << index);
};

inline bool check_test(uint16_t test, size_t index)
{
	return test & (0x1u << index);
};

template <size_t I>
inline bool check_test(const vfloat<I> &test, size_t index)
{
	return reinterpret_cast<int32_t *>(&test)[index];
};

template <size_t I>
struct ntri : public nprim<I>
{
	vfloat<I> origx, origy, origz;
	vfloat<I> edge1x, edge1y, edge1z;
	vfloat<I> edge2x, edge2y, edge2z;

	ntri(const std::vector<boost::shared_ptr<Primitive>> &p) : nprim<I>(p)
	{
		for(size_t i = 0; i < I; i++)
		{
			const MeshBaryTriangle *t = static_cast<const MeshBaryTriangle *>(p[i].get());
			origx[i] = t->GetP(0).x;
			origy[i] = t->GetP(0).y;
			origz[i] = t->GetP(0).z;
			edge1x[i] = t->GetP(1).x - t->GetP(0).x;
			edge1y[i] = t->GetP(1).y - t->GetP(0).y;
			edge1z[i] = t->GetP(1).z - t->GetP(0).z;
			edge2x[i] = t->GetP(2).x - t->GetP(0).x;
			edge2y[i] = t->GetP(2).y - t->GetP(0).y;
			edge2z[i] = t->GetP(2).z - t->GetP(0).z;
		}
	};

	virtual ~ntri()
	{

	};

	bool Intersect(const nray<I> &ray4, const Ray &ray, Intersection *isect) const
	{
		const vfloat<I> zero = 0.0f;
		const vfloat<I> s1x = ((ray4.dy * edge2z) - (ray4.dz * edge2y));
		const vfloat<I> s1y = ((ray4.dz * edge2x) - (ray4.dx * edge2z));
		const vfloat<I> s1z = ((ray4.dx * edge2y) - (ray4.dy * edge2x));
		const vfloat<I> divisor = ((s1x * edge1x) + ((s1y * edge1y) + (s1z * edge1z)));
		auto test = vneq(divisor,zero);
//		const vfloat<I> inverse = reciprocal(divisor);
		const vfloat<I> dx = (ray4.ox - origx);
		const vfloat<I> dy = (ray4.oy - origy);
		const vfloat<I> dz = (ray4.oz - origz);
		const vfloat<I> b1 = (((dx * s1x) + ((dy * s1y) + (dz * s1z))) / divisor);
		test &= vge(b1,zero);
		const vfloat<I> s2x = ((dy * edge1z) - (dz * edge1y));
		const vfloat<I> s2y = ((dz * edge1x) - (dx * edge1z));
		const vfloat<I> s2z = ((dx * edge1y) - (dy * edge1x));
		const vfloat<I> b2 = (((ray4.dx * s2x) + ((ray4.dy * s2y) + (ray4.dz * s2z))) / divisor);
		const vfloat<I> b0 = (1.0f - (b1 + b2));
		test &= vge(b2,zero) & vge(b0,zero);
		const vfloat<I> t = (((edge2x * s2x) + ((edge2y * s2y) + (edge2z * s2z))) / divisor);
		test &= vgt(t, ray4.mint) & vlt(t, ray4.maxt);
		
		size_t hit = I;
		for(size_t i = 0; i < I; i++)
		{
			if(check_test(test,i) && (t[i] < ray.maxt))
			{
				hit = i;
				ray.maxt = t[i];
			}
		}

		if(hit == I)
			return false;

		ray4.maxt = ray.maxt;

		const MeshBaryTriangle *triangle(static_cast<const MeshBaryTriangle *>(
			this->primitives[hit].get()
		));

		const Point o(origx[hit],origy[hit],origz[hit]);
		const Vector e1(edge1x[hit],edge1y[hit],edge1z[hit]);
		const Vector e2(edge2x[hit],edge2y[hit],edge2z[hit]);
		const float _b0 = b0[hit];
		const float _b1 = b1[hit];
		const float _b2 = b2[hit];
		const Normal nn(Normalize(Cross(e1, e2)));
		const Point pp(o + _b1 * e1 + _b2 * e2);

		// Fill in _DifferentialGeometry_ from triangle hit
		// Compute triangle partial derivatives
		Vector dpdu, dpdv;
		float uvs[3][2];
		triangle->GetUVs(uvs);

		// Compute deltas for triangle partial derivatives
		const float du1 = uvs[0][0] - uvs[2][0];
		const float du2 = uvs[1][0] - uvs[2][0];
		const float dv1 = uvs[0][1] - uvs[2][1];
		const float dv2 = uvs[1][1] - uvs[2][1];
		const Vector dp1 = triangle->GetP(0) - triangle->GetP(2);
		const Vector dp2 = triangle->GetP(1) - triangle->GetP(2);

		const float determinant = du1 * dv2 - dv1 * du2;
		if(determinant == 0.f)
		{
			// Handle zero determinant for triangle partial derivative matrix
			CoordinateSystem(Vector(nn), &dpdu, &dpdv);
		}
		else
		{
			const float invdet = 1.f / determinant;
			dpdu = ( dv2 * dp1 - dv1 * dp2) * invdet;
			dpdv = (-du2 * dp1 + du1 * dp2) * invdet;
		}

		// Interpolate $(u,v)$ triangle parametric coordinates
		const float tu = _b0 * uvs[0][0] + _b1 * uvs[1][0] +
			_b2 * uvs[2][0];
		const float tv = _b0 * uvs[0][1] + _b1 * uvs[1][1] +
			_b2 * uvs[2][1];

		isect->dg = DifferentialGeometry(pp, nn, dpdu, dpdv,
			Normal(0, 0, 0), Normal(0, 0, 0), tu, tv, triangle);

		isect->Set(triangle->mesh->ObjectToWorld, triangle,
			triangle->mesh->GetMaterial(),
			triangle->mesh->GetExterior(),
			triangle->mesh->GetInterior());
		isect->dg.iData.baryTriangle.coords[0] = _b0;
		isect->dg.iData.baryTriangle.coords[1] = _b1;
		isect->dg.iData.baryTriangle.coords[2] = _b2;

		return true;
	}
};

template <size_t I>
struct nbvh_node
{
	static const int32_t emptyLeafNode = 0xffffffff;
	//static const int32_t leafNode = 0x1lu << 63lu;
	//static const int32_t fistQuadIndex = 0x07ffffffffffffff;

	vfloat<I> bboxes[2][3];
	int32_t children[I];

	/*
		children are indices
		bit 0 determines if child is a leaf node
		bits 1-4 encode the number of primitives in the leaf
	*/

	inline nbvh_node()
	{
		for(size_t i = 0; i < 3; i++)
		{
			bboxes[0][i] = INFINITY;
			bboxes[1][i] = -INFINITY;
		}

		for(size_t i = 0; i < I; i++)
			children[i] = emptyLeafNode;
	};

	inline bool ChildIsLeaf(int32_t i) const {return (children[i] < 0);};

	inline static bool IsLeaf(int32_t index) {return (index < 0);};

	inline bool LeafIsEmpty(int32_t i) const {return (children[i] == emptyLeafNode);};

	inline static bool IsEmpty(int32_t index) {return (index == emptyLeafNode);};

	inline u_int NbQuadsInLeaf(int32_t i) const
	{return static_cast<u_int>((children[i] >> 27) & 0xf) + 1;};

	inline static u_int NbQuadPrimitives(int32_t index)
	{return static_cast<u_int>((index >> 27) & 0xf) + 1;};
	
	inline u_int NbPrimitivesInLeaf(int32_t i) const {return NbQuadsInLeaf(i) * I;};

	inline u_int FirstQuadIndexForLeaf(int32_t i) const {return children[i] & 0x07ffffff;};
	
	inline static u_int FirstQuadIndex(int32_t index) {return index & 0x07ffffff;};

	inline void InitializeLeaf(int32_t i, u_int nbQuads, u_int firstQuadIndex)
	{
		// Take care to make a valid initialisation of the leaf.
		if(nbQuads == 0)
		{
			children[i] = emptyLeafNode;
		}
		else
		{
			// Put the negative sign in a plateform independent way
			children[i] = 0x80000000;//-1L & ~(-1L >> 1L);
			
			children[i] |=  ((static_cast<int32_t>(nbQuads) - 1) & 0xf) << 27;

			children[i] |= static_cast<int32_t>(firstQuadIndex) & 0x07ffffff;
		}
	}

	inline void SetBBox(u_int i, const BBox &bbox)
	{
		for(size_t axis = 0; axis < 3; ++axis)
		{
			bboxes[0][axis][i] = bbox.pMin[axis];
			bboxes[1][axis][i] = bbox.pMax[axis];
		}
	};

	inline BBox GetBBox(u_int i) const
	{
		BBox bbox;
		bbox.pMin.x = bboxes[0][0][i];
		bbox.pMax.x = bboxes[1][0][i];
		bbox.pMin.y = bboxes[0][1][i];
		bbox.pMax.y = bboxes[1][1][i];
		bbox.pMin.z = bboxes[0][2][i];
		bbox.pMax.z = bboxes[1][2][i];
		return bbox;
	};

	int32_t BBoxIntersect(const nray<I> &ray4, const vfloat<I> invDir[3], const int sign[3]) const
	{
		vfloat<I> tMin = ray4.mint;
		vfloat<I> tMax = ray4.maxt;

		// X coordinate
		tMin = vmax(tMin,((bboxes[sign[0]][0] - ray4.ox) * invDir[0]));
		tMax = vmin(tMax,((bboxes[1 - sign[0]][0] - ray4.ox) * invDir[0]));

		// Y coordinate
		tMin = vmax(tMin,((bboxes[sign[1]][1] - ray4.oy) * invDir[1]));
		tMax = vmin(tMax,((bboxes[1 - sign[1]][1] - ray4.oy) * invDir[1]));

		// Z coordinate
		tMin = vmax(tMin,((bboxes[sign[2]][2] - ray4.oz) * invDir[2]));
		tMax = vmin(tMax,((bboxes[1 - sign[2]][2] - ray4.oz) * invDir[2]));

		//return the visit flags
		return vge(tMax,tMin); //TODO: convert to mask if needed
	};
};

template <size_t I>
struct nbvh_accel : public lux::Aggregate
{
	static const int16_t pathTable[128];

	u_int nQuads = 0;

	/**
	   The primitive associated with each triangle. indexed by the number of quad
	   and the number of triangle in the quad (thus, there might be holes).
	   no need to be a tessellated primitive, the intersection
	   test will be redone for the nearest triangle found, to
	   fill the Intersection structure.
	*/
	boost::shared_ptr<nprim<I>> *prims = nullptr;
	u_int nPrims = 0; //The number of primitives
	nbvh_node<I>* nodes = nullptr;
	u_int nNodes = 0; //The number of nodes really used.
	u_int maxNodes = 0;
	BBox worldBound; //The world bounding box of the QBVH.
	u_int fullSweepThreshold = 0; //num of prims in node that makes switch to full sweep for binning
	u_int skipFactor = 0; //The skip factor for binning
	u_int maxPrimsPerLeaf = 0; //max number of primitives per leaf

	// Some statistics about the quality of the built accelerator
	float SAHCost = 0.0f;
	float avgLeafPrimReferences = 0.0f;
	u_int maxDepth = 0;
	u_int nodeCount = 0;
	u_int noEmptyLeafCount = 0;
	u_int emptyLeafCount = 0;
	u_int primReferences = 0;

	/**
	   Normal constructor.
	   @param p the vector of shared primitives to put in the QBVH
	   @param mp the maximum number of primitives per leaf
	   @param fst the threshold before switching to full sweep for split
	   @param sf the skip factor during split determination
	*/
	nbvh_accel(const vector<boost::shared_ptr<Primitive>> &p, u_int mp, u_int fst, u_int sf) :
		fullSweepThreshold(fst), skipFactor(sf), maxPrimsPerLeaf(mp)
	{
		vector<boost::shared_ptr<Primitive> > vPrims;
		const PrimitiveRefinementHints refineHints(false);
		for(u_int i = 0; i < p.size(); ++i) {
			if(p[i]->CanIntersect())
				vPrims.push_back(p[i]);
			else
				p[i]->Refine(vPrims, refineHints, p[i]);
		}

		// Initialize primitives for _QBVHAccel_
		nPrims = vPrims.size();

		// Temporary data for building
		u_int* primsIndexes = new u_int[nPrims + 3]; // For the case where
		// the last quad would begin at the last primitive
		// (or the second or third last primitive)

		// The number of nodes depends on the number of primitives,
		// and is bounded by 2 * nPrims - 1.
		// Even if there will normally have at least 4 primitives per leaf,
		// it is not always the case => continue to use the normal bounds.
		nNodes = 0;
		maxNodes = 1;
		for(
			u_int layer = ((nPrims + maxPrimsPerLeaf - 1) / maxPrimsPerLeaf + (I-1)) / I;
			layer > 1; layer = (layer + (I-1)) / I
		)
			maxNodes += layer;
		nodes = AllocAligned<nbvh_node<I>>(maxNodes);
		for(u_int i = 0; i < maxNodes; ++i)
			nodes[i] = nbvh_node<I>();

		// The arrays that will contain
		// - the bounding boxes for all triangles
		// - the centroids for all triangles	
		BBox* primsBboxes = new BBox[nPrims];
		Point* primsCentroids = new Point[nPrims];
		// The bouding volume of all the centroids
		BBox centroidsBbox;
		
		// Fill each base array
		for(u_int i = 0; i < nPrims; ++i)
		{
			// This array will be reorganized during construction. 
			primsIndexes[i] = i;

			// Compute the bounding box for the triangle
			primsBboxes[i] = vPrims[i]->WorldBound();
			primsBboxes[i].Expand(MachineEpsilon::E(primsBboxes[i]));
			primsCentroids[i] = (primsBboxes[i].pMin + primsBboxes[i].pMax) * 0.5f;

			// Update the global bounding boxes
			worldBound = Union(worldBound, primsBboxes[i]);
			centroidsBbox = Union(centroidsBbox, primsCentroids[i]);
		}

		// Arbitrarily take the last primitive for the last I-1
		for(size_t i = 0; i < (I-1); i++)
			primsIndexes[nPrims+i] = nPrims - 1;

		// Recursively build the tree
		LOG(LUX_DEBUG,LUX_NOERROR) << "Building QBVH, primitives: " << nPrims << 
			", initial nodes: " << maxNodes;
		nQuads = 0;
		BuildTree(0, nPrims, primsIndexes, primsBboxes, primsCentroids,
			worldBound, centroidsBbox, -1, 0, 0);

		prims = AllocAligned<boost::shared_ptr<nprim<I>> >(nQuads);
		nQuads = 0;
		PreSwizzle(0, primsIndexes, vPrims);
		LOG(LUX_DEBUG,LUX_NOERROR) << "QBVH completed with " << nNodes << "/" << maxNodes << " nodes";
		
		// Collect statistics
		maxDepth = 0;
		nodeCount = 0;
		noEmptyLeafCount = 0;
		emptyLeafCount = 0;
		primReferences = 0;
		//SAHCost = CollectStatistics(0, 0, worldBound);
		avgLeafPrimReferences = primReferences / (noEmptyLeafCount > 0 ? noEmptyLeafCount : 1);
		
		// Print the statistics
		LOG(LUX_DEBUG, LUX_NOERROR) << "QBVH SAH total cost: " << SAHCost;
		LOG(LUX_DEBUG, LUX_NOERROR) << "QBVH max. depth: " << maxDepth;
		LOG(LUX_DEBUG, LUX_NOERROR) << "QBVH node count: " << nodeCount;
		LOG(LUX_DEBUG, LUX_NOERROR) << "QBVH empty leaf count: " << emptyLeafCount;
		LOG(LUX_DEBUG, LUX_NOERROR) << "QBVH not empty leaf count: " << noEmptyLeafCount;
		LOG(LUX_DEBUG, LUX_NOERROR) << "QBVH avg. primitive references per leaf: " <<avgLeafPrimReferences;
		LOG(LUX_DEBUG, LUX_NOERROR) << "QBVH primitive references: " << primReferences << "/" << nPrims;
		
		// Release temporary memory
		delete[] primsBboxes;
		delete[] primsCentroids;
		delete[] primsIndexes;
	};

	virtual ~nbvh_accel()
	{
		for(size_t i = 0; i < nQuads; ++i)
			prims[i].~shared_ptr();
		FreeAligned(prims);
		FreeAligned(nodes);
	};

	void BuildTree(
		u_int start, u_int end, u_int *primsIndexes,
		const BBox *primsBboxes, const Point *primsCentroids, const BBox &nodeBbox,
		const BBox &centroidsBbox, int32_t parentIndex, int32_t childIndex, int depth
	)
	{
		// Create a leaf ?
		//********
		if(depth > 64 || end - start <= maxPrimsPerLeaf)
		{
			if(depth > 64)
			{
				LOG(LUX_WARNING, LUX_LIMIT) << "Maximum recursion depth reached while "
				"constructing QBVH, forcing a leaf node";
				if(end - start > 64)
				{
					LOG(LUX_ERROR, LUX_LIMIT) << "QBVH unable to handle geometry, too "
					"many primitives in leaf";
					end = start + 64;
				}
			}
			CreateTempLeaf(parentIndex, childIndex, start, end, nodeBbox);
			return;
		}

		// Look for the split position
		int axis = 0;
		float splitPos = BuildObjectSplit(start, end, primsIndexes, primsBboxes,
			primsCentroids, centroidsBbox, axis);
		
		if(isnan(splitPos))
		{
			if(end - start > 64)
			{
				LOG(LUX_ERROR, LUX_LIMIT) << "QBVH unable to handle geometry, too many primitives "
				"with the same centroid";
				end = start + 64;
			}
			CreateTempLeaf(parentIndex, childIndex, start, end, nodeBbox);
			return;
		}

		BBox leftChildBbox, rightChildBbox;
		BBox leftChildCentroidsBbox, rightChildCentroidsBbox;

		u_int storeIndex = start;
		for(u_int i = start; i < end; i++)
		{
			const u_int primIndex = primsIndexes[i];

			// This test isn't really correct because produces different results from
			// the one in BuildObjectSplit(). For instance, it happens when the centroid
			// is exactly on the split. SQBVH uses the right approach. However, this
			// kind of problem has no side effects in a pure QBVH so it is not worth
			// fixing here.
			if(primsCentroids[primIndex][axis] <= splitPos)
			{
				// Swap
				primsIndexes[i] = primsIndexes[storeIndex];
				primsIndexes[storeIndex] = primIndex;
				++storeIndex;
				
				// Update the bounding boxes,
				// this triangle is on the left side
				leftChildBbox = Union(leftChildBbox, primsBboxes[primIndex]);
				leftChildCentroidsBbox = Union(leftChildCentroidsBbox, primsCentroids[primIndex]);
			}
			else
			{
				// Update the bounding boxes,
				// this triangle is on the right side.
				rightChildBbox = Union(rightChildBbox, primsBboxes[primIndex]);
				rightChildCentroidsBbox = Union(rightChildCentroidsBbox, primsCentroids[primIndex]);
			}
		}

		int32_t currentNode = parentIndex;
		int32_t leftChildIndex = childIndex;
		int32_t rightChildIndex = childIndex + 1;

		// Create an intermediate node if the depth indicates to do so.
		// Register the split axis.
		if(depth % 2 == 0)
		{
			currentNode = CreateIntermediateNode(parentIndex, childIndex, nodeBbox);
			leftChildIndex = 0;
			rightChildIndex = 2;
		}

		// Build recursively
		BuildTree(start, storeIndex, primsIndexes, primsBboxes, primsCentroids,
			leftChildBbox, leftChildCentroidsBbox, currentNode,
			leftChildIndex, depth + 1);
		BuildTree(storeIndex, end, primsIndexes, primsBboxes, primsCentroids,
			rightChildBbox, rightChildCentroidsBbox, currentNode,
			rightChildIndex, depth + 1);
	};

	inline int32_t CreateIntermediateNode(int32_t parentIndex, int32_t childIndex, const BBox &nodeBbox)
	{
		int32_t index = nNodes++; // increment after assignment
		if(nNodes >= maxNodes)
		{
			nbvh_node<I> *newNodes = luxrays::AllocAligned<nbvh_node<I>>(2 * maxNodes);
			memcpy(newNodes, nodes, sizeof(nbvh_node<I>) * maxNodes);
			for(size_t i = 0; i < maxNodes; ++i)
				newNodes[maxNodes + i] = nbvh_node<I>();
			luxrays::FreeAligned(nodes);
			nodes = newNodes;
			maxNodes *= 2;
		}

		if(parentIndex >= 0)
		{
			nodes[parentIndex].children[childIndex] = index;
			nodes[parentIndex].SetBBox(childIndex, nodeBbox);
		}
		return index;
	};

	static inline u_int QuadCount(const u_int nPrims) {
		// Next multiple of 4, divided by 4
		return (nPrims + (I-1)) / I;
	};

	float BuildObjectSplit(
		const u_int start, const u_int end,
		const u_int *primsIndexes, const BBox *primsBboxes,
		const Point *primsCentroids, const BBox &centroidsBbox, int &axis
	)
	{
		// Choose the split axis, taking the axis of maximum extent for the
		// centroids (else weird cases can occur, where the maximum extent axis
		// for the nodeBbox is an axis of 0 extent for the centroids one.).
		axis = centroidsBbox.MaximumExtent();

		// Precompute values that are constant with respect to the current
		// primitive considered.
		const float k0 = centroidsBbox.pMin[axis];
		const float k1 = OBJECT_SPLIT_BINS / (centroidsBbox.pMax[axis] - k0);
		
		// If the bbox is a point
		if(isinf(k1))
			return std::numeric_limits<float>::quiet_NaN();

		// Number of primitives in each bin
		int bins[OBJECT_SPLIT_BINS];
		// Bbox of the primitives in the bin
		BBox binsBbox[OBJECT_SPLIT_BINS];

		//--------------
		// Fill in the bins, considering all the primitives when a given
		// threshold is reached, else considering only a portion of the
		// primitives for the binned-SAH process. Also compute the bins bboxes
		// for the primitives. 

		for(size_t i = 0; i < OBJECT_SPLIT_BINS; ++i)
			bins[i] = 0.f;

		u_int step = (end - start < fullSweepThreshold) ? 1 : skipFactor;

		for(size_t i = start; i < end; i += step)
		{
			const u_int primIndex = primsIndexes[i];
			
			// Binning is relative to the centroids bbox and to the
			// primitives' centroid.
			const int binId = max(0, min(OBJECT_SPLIT_BINS - 1,
					Floor2Int(k1 * (primsCentroids[primIndex][axis] - k0))));
			bins[binId]++;
			binsBbox[binId] = Union(binsBbox[binId], primsBboxes[primIndex]);
		}

		//--------------
		// Evaluate where to split.

		// Cumulative number of primitives in the bins from the first to the
		// ith, and from the last to the ith.
		int nbPrimsLeft[OBJECT_SPLIT_BINS];
		int nbPrimsRight[OBJECT_SPLIT_BINS];
		// The corresponding cumulative bounding boxes.
		BBox bboxesLeft[OBJECT_SPLIT_BINS];
		BBox bboxesRight[OBJECT_SPLIT_BINS];

		// The corresponding SAHs
		float areaLeft[OBJECT_SPLIT_BINS];
		float areaRight[OBJECT_SPLIT_BINS];	

		BBox currentBboxLeft, currentBboxRight;
		int currentNbLeft = 0, currentNbRight = 0;

		for(int i = 0; i < OBJECT_SPLIT_BINS; ++i)
		{
			//-----
			// Left side
			// Number of prims
			currentNbLeft += bins[i];
			nbPrimsLeft[i] = currentNbLeft;
			// Prims bbox
			currentBboxLeft = Union(currentBboxLeft, binsBbox[i]);
			bboxesLeft[i] = currentBboxLeft;
			// Surface area
			areaLeft[i] = currentBboxLeft.SurfaceArea();
			
			//-----
			// Right side
			// Number of prims
			const int rightIndex = OBJECT_SPLIT_BINS - 1 - i;
			currentNbRight += bins[rightIndex];
			nbPrimsRight[rightIndex] = currentNbRight;
			// Prims bbox
			currentBboxRight = Union(currentBboxRight, binsBbox[rightIndex]);
			bboxesRight[rightIndex] = currentBboxRight;
			// Surface area
			areaRight[rightIndex] = currentBboxRight.SurfaceArea();
		}

		int minBin = -1;
		float minCost = INFINITY;
		// Find the best split axis,
		// there must be at least a bin on the right side
		for(int i = 0; i < OBJECT_SPLIT_BINS - 1; ++i)
		{
			float cost = areaLeft[i] * QuadCount(nbPrimsLeft[i]) +
				areaRight[i + 1] * QuadCount(nbPrimsRight[i + 1]);
			
			if(cost < minCost)
			{
				minBin = i;
				minCost = cost;
			}
		}

		//-----------------
		// Make the partition, in a "quicksort partitioning" way,
		// the pivot being the position of the split plane
		// (no more binId computation)
		// track also the bboxes (primitives and centroids)
		// for the left and right halves.

		// The split plane coordinate is the coordinate of the end of
		// the chosen bin along the split axis
		return centroidsBbox.pMin[axis] + (minBin + 1) *
			(centroidsBbox.pMax[axis] - centroidsBbox.pMin[axis]) / OBJECT_SPLIT_BINS;
	};

/***************************************************/
	void CreateTempLeaf(\
		int32_t parentIndex, int32_t childIndex,
		u_int start, u_int end, const BBox &nodeBbox
	)
	{
		// The leaf is directly encoded in the intermediate node.
		if (parentIndex < 0) {
			// The entire tree is a leaf
			nNodes = 1;
			parentIndex = 0;
		}

		// Encode the leaf in the original way,
		// it will be transformed to a preswizzled format in a post-process.
		
		u_int nbPrimsTotal = end - start;
		
		nbvh_node<I> &node = nodes[parentIndex];

		node.SetBBox(childIndex, nodeBbox);

		u_int quads = QuadCount(nbPrimsTotal);
		
		// Use the same encoding as the final one, but with a different meaning.
		node.InitializeLeaf(childIndex, quads, start);

		nQuads += quads;
	};

	void PreSwizzle(int32_t nodeIndex, const u_int *primsIndexes,
		const vector<boost::shared_ptr<Primitive> > &vPrims)
	{
		for(int i = 0; i < I; ++i)
		{
			if(nodes[nodeIndex].ChildIsLeaf(i))
				CreateSwizzledLeaf(nodeIndex, i, primsIndexes, vPrims);
			else
				PreSwizzle(nodes[nodeIndex].children[i], primsIndexes, vPrims);
		}
	};

	void CreateSwizzledLeaf(
		int32_t parentIndex, int32_t childIndex,
		const u_int *primsIndexes, const vector<boost::shared_ptr<Primitive>> &vPrims
	)
	{
		nbvh_node<I> &node = nodes[parentIndex];
		if(node.LeafIsEmpty(childIndex))
			return;
		const u_int startQuad = nQuads;
		const u_int nbQuads = node.NbQuadsInLeaf(childIndex);

		u_int primOffset = node.FirstQuadIndexForLeaf(childIndex);
		u_int primNum = nQuads;

		for(size_t q = 0; q < nbQuads; ++q)
		{
			bool allTri = true;
			for(size_t i = 0; i < I; ++i)
			{
				allTri &= dynamic_cast<MeshBaryTriangle*>(
					vPrims[primsIndexes[primOffset + i]].get()
				) != nullptr;
			}

			std::vector<boost::shared_ptr<Primitive>> pvec = {};
			for(size_t j = 0; j < I; j++)
				pvec.push_back(vPrims[primsIndexes[primOffset+j]]);

			if(allTri)
			{
				boost::shared_ptr<nprim<I>> p(new ntri<I>(pvec));
				new (&prims[primNum]) boost::shared_ptr<nprim<I>>(p);
			}
			else
			{
				boost::shared_ptr<nprim<I>> p(new nprim<I>(pvec));
				new (&prims[primNum]) boost::shared_ptr<nprim<I>>(p);
			}
			++primNum;
			primOffset += I;
		}
		nQuads += nbQuads;
		node.InitializeLeaf(childIndex, nbQuads, startQuad);
	};
	
	virtual BBox WorldBound() const
	{
		return worldBound;
	};

	virtual bool Intersect(const Ray &ray, Intersection *isect) const
	{
		nray<I> ray4(ray);
		vfloat<I> invDir[3];
		invDir[0] = (1.f / ray.d.x);
		invDir[1] = (1.f / ray.d.y);
		invDir[2] = (1.f / ray.d.z);

		int signs[3];
		ray.GetDirectionSigns(signs);

		//------------------------------
		// Main loop
		bool hit = false;
		// The nodes stack, 256 nodes should be enough
		int todoNode = 0; // the index in the stack
		int32_t nodeStack[64];
		nodeStack[0] = 0; // first node to handle: root node
		
		while(todoNode >= 0)
		{
			// Leaves are identified by a negative index
			if(!nbvh_node<I>::IsLeaf(nodeStack[todoNode]))
			{
				nbvh_node<I> &node = nodes[nodeStack[todoNode]];
				--todoNode;
				
				const int32_t visit = node.BBoxIntersect(ray4,invDir,signs);

				if(visit & 0x1)
					nodeStack[++todoNode] = node.children[0];
				if(visit & 0x2)
					nodeStack[++todoNode] = node.children[1];
				if(visit & 0x4)
					nodeStack[++todoNode] = node.children[2];
				if(visit & 0x8)
					nodeStack[++todoNode] = node.children[3];
			}
			else
			{
				//----------------------
				// It is a leaf,
				// all the informations are encoded in the index
				const int32_t leafData = nodeStack[todoNode];
				--todoNode;
				
				if(nbvh_node<I>::IsEmpty(leafData))
					continue;

				// Perform intersection
				const u_int nbQuadPrimitives = nbvh_node<I>::NbQuadPrimitives(leafData);
				
				const u_int offset = nbvh_node<I>::FirstQuadIndex(leafData);

				for(u_int primNumber = offset; primNumber < (offset + nbQuadPrimitives); ++primNumber)
					hit |= prims[primNumber]->Intersect(ray4, ray, isect);
			}//end of the else
		}

		return hit;
	};

	virtual bool IntersectP(const Ray &ray) const
	{
		nray<I> ray4(ray);
		vfloat<I> invDir[3];
		invDir[0] = (1.f / ray.d.x);
		invDir[1] = (1.f / ray.d.y);
		invDir[2] = (1.f / ray.d.z);

		int signs[3];
		ray.GetDirectionSigns(signs);

		//------------------------------
		// Main loop
		// The nodes stack, 256 nodes should be enough
		int todoNode = 0; // the index in the stack
		int32_t nodeStack[64];
		nodeStack[0] = 0; // first node to handle: root node

		while(todoNode >= 0)
		{
			// Leaves are identified by a negative index
			if(!nbvh_node<I>::IsLeaf(nodeStack[todoNode]))
			{
				nbvh_node<I> &node = nodes[nodeStack[todoNode]];
				--todoNode;

				const int32_t visit = node.BBoxIntersect(ray4, invDir,
					signs);

				if(visit & 0x1)
					nodeStack[++todoNode] = node.children[0];
				if(visit & 0x2)
					nodeStack[++todoNode] = node.children[1];
				if(visit & 0x4)
					nodeStack[++todoNode] = node.children[2];
				if(visit & 0x8)
					nodeStack[++todoNode] = node.children[3];
			}
			else
			{
				//----------------------
				// It is a leaf,
				// all the informations are encoded in the index
				const int32_t leafData = nodeStack[todoNode];
				--todoNode;
				
				if(nbvh_node<I>::IsEmpty(leafData))
					continue;

				// Perform intersection
				const u_int nbQuadPrimitives = nbvh_node<I>::NbQuadPrimitives(leafData);
				
				const u_int offset = nbvh_node<I>::FirstQuadIndex(leafData);

				for(u_int primNumber = offset; primNumber < (offset + nbQuadPrimitives); ++primNumber)
				{
					if(prims[primNumber]->IntersectP(ray))
						return true;
				}
			} // end of the else
		}

		return false;
	};

	virtual Transform GetLocalToWorld(float time) const
	{
		return Transform();
	};

	virtual void GetPrimitives(vector<boost::shared_ptr<Primitive>> &primitives) const
	{
		primitives.reserve(primitives.size() + nPrims);
		for(u_int i = 0; i < nPrims; i++)
			primitives.push_back(prims[i]);
		for(u_int i = 0; i < nPrims; i++)
			prims[i]->GetPrimitives(primitives);
	};

	static Aggregate *CreateAccelerator(
		const vector<boost::shared_ptr<Primitive> > &prims, const ParamSet &ps
	)
	{
		int maxPrimsPerLeaf = ps.FindOneInt("maxprimsperleaf", I);
		int fullSweepThreshold = ps.FindOneInt("fullsweepthreshold", I * maxPrimsPerLeaf);
		int skipFactor = ps.FindOneInt("skipfactor", 1);
		return new nbvh_accel(prims, maxPrimsPerLeaf, fullSweepThreshold, skipFactor);
	};
};