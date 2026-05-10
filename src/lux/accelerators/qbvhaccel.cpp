/***************************************************************************
 *   Copyright (C) 2007 by Anthony Pajot   
 *   anthony.pajot@etu.enseeiht.fr
 *
 * This file is part of FlexRay
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 ***************************************************************************/

#include "qbvhaccel.h"
#include "shapes/mesh.h"
#include "paramset.h"
#include "dynload.h"
#include "error.h"
#include "simd.h"

using namespace luxrays;

namespace lux
{

#if defined(WIN32) && !defined(__CYGWIN__)
class __declspec(align(16)) QuadRay {
#else 
class QuadRay {
#endif
public:
	QuadRay(const Ray &ray)
	{
		ox = ray.o.x;
		oy = ray.o.y;
		oz = ray.o.z;
		dx = ray.d.x;
		dy = ray.d.y;
		dz = ray.d.z;
		mint = ray.mint;
		maxt = ray.maxt;
	}

	vfloat<4> ox, oy, oz;
	vfloat<4> dx, dy, dz;
	mutable vfloat<4> mint, maxt;
#if defined(WIN32) && !defined(__CYGWIN__)
};
#else 
} __attribute__ ((aligned(16)));
#endif 

class QuadPrimitive : public Aggregate {
public:
	// Don't use references to force temporaries and increase use count
	QuadPrimitive(boost::shared_ptr<Primitive> p1,
		boost::shared_ptr<Primitive> p2,
		boost::shared_ptr<Primitive> p3,
		boost::shared_ptr<Primitive> p4) {
		primitives[0] = p1;
		primitives[1] = p2;
		primitives[2] = p3;
		primitives[3] = p4;
	}
	virtual ~QuadPrimitive() { }
	virtual BBox WorldBound() const
	{
		return Union(Union(primitives[0]->WorldBound(),
			primitives[1]->WorldBound()),
			Union(primitives[2]->WorldBound(),
			primitives[3]->WorldBound()));
	}
	virtual bool Intersect(const Ray &ray, Intersection *isect) const
	{
		bool hit = false;
		for (u_int i = 0; i < 4; ++i)
			hit |= primitives[i]->Intersect(ray, isect);
		return hit;
	}
	virtual bool IntersectP(const Ray &ray) const
	{
		for (u_int i = 0; i < 4; ++i)
			if (primitives[i]->IntersectP(ray))
				return true;
		return false;
	}
	virtual Transform GetLocalToWorld(float time) const {
		return Transform();
	}
	virtual void GetPrimitives(vector<boost::shared_ptr<Primitive> > &prims) const
	{
		prims.reserve(prims.size() + 4);
		for (u_int i = 0; i < 4; ++i)
			prims.push_back(primitives[i]);
	}
	virtual bool Intersect(const QuadRay &ray4, const Ray &ray, Intersection *isect) const
	{
		const bool hit = Intersect(ray, isect);
		if (!hit)
			return false;
		ray4.maxt = ray.maxt;
		return true;
	}
protected:
	boost::shared_ptr<Primitive> primitives[4];
};

static inline vfloat<4> reciprocal(const vfloat<4> &x)
{
	vfloat<4> y;
	y.as<__m128>() = _mm_rcp_ps(x.as<__m128>());
	return y * (2.f - (x * y));
}

class QuadTriangle : public QuadPrimitive, public Aligned16
{
public:
	QuadTriangle(const boost::shared_ptr<Primitive> &p1,
		const boost::shared_ptr<Primitive> &p2,
		const boost::shared_ptr<Primitive> &p3,
		const boost::shared_ptr<Primitive> &p4) :
		QuadPrimitive(p1, p2, p3, p4)
	{
		for (u_int i = 0; i < 4; ++i) {
			const MeshBaryTriangle *t = static_cast<const MeshBaryTriangle *>(primitives[i].get());
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
	}
	virtual ~QuadTriangle() { }
	virtual bool Intersect(const QuadRay &ray4, const Ray &ray, Intersection *isect) const
	{
		const vfloat<4> zero = 0.f;
		const vfloat<4> s1x = ((ray4.dy * edge2z) - (ray4.dz * edge2y));
		const vfloat<4> s1y = ((ray4.dz * edge2x) - (ray4.dx * edge2z));
		const vfloat<4> s1z = ((ray4.dx * edge2y) - (ray4.dy * edge2x));
		const vfloat<4> divisor = ((s1x * edge1x) + ((s1y * edge1y) + (s1z * edge1z)));
		vfloat<4> test;
		test.as<__m128>() = _mm_cmpneq_ps(divisor.as<__m128>(), zero.as<__m128>());
//		const __m128 inverse = reciprocal(divisor);
		const vfloat<4> dx = (ray4.ox - origx);
		const vfloat<4> dy = (ray4.oy - origy);
		const vfloat<4> dz = (ray4.oz - origz);
		const vfloat<4> b1 = (((dx * s1x) + ((dy * s1y) + (dz * s1z))) / divisor);
		test.as<__m128>() = _mm_and_ps(test.as<__m128>(), _mm_cmpge_ps(b1.as<__m128>(), zero.as<__m128>()));
		const vfloat<4> s2x = ((dy * edge1z) - (dz * edge1y));
		const vfloat<4> s2y = ((dz * edge1x) - (dx * edge1z));
		const vfloat<4> s2z = ((dx * edge1y) - (dy * edge1x));
		const vfloat<4> b2 = (((ray4.dx * s2x) + ((ray4.dy * s2y) + (ray4.dz * s2z))) / divisor);
		const vfloat<4> b0 = (1.f - (b1 + b2));
		test.as<__m128>() = _mm_and_ps(test.as<__m128>(),_mm_and_ps(
			_mm_cmpge_ps(b2.as<__m128>(), zero.as<__m128>()),
			_mm_cmpge_ps(b0.as<__m128>(), zero.as<__m128>())
		));
		const vfloat<4> t = (((edge2x * s2x) + ((edge2y * s2y) + (edge2z * s2z))) / divisor);
		test.as<__m128>() = _mm_and_ps(test.as<__m128>(), _mm_and_ps(
			_mm_cmpgt_ps(t.as<__m128>(), ray4.mint.as<__m128>()),
			_mm_cmplt_ps(t.as<__m128>(), ray4.maxt.as<__m128>())
		));
		
		size_t hit = 4;
		for(size_t i = 0; i < 4; ++i)
		{
			if (reinterpret_cast<int32_t *>(&test)[i] && t[i] < ray.maxt)
			{
				hit = i;
				ray.maxt = t[i];
			}
		}

		if(hit == 4)
			return false;

		ray4.maxt = ray.maxt;

		const MeshBaryTriangle *triangle(static_cast<const MeshBaryTriangle *>(primitives[hit].get()));

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
		const Vector dp1(triangle->GetP(0) - triangle->GetP(2)),
		      dp2(triangle->GetP(1) - triangle->GetP(2));

		const float determinant = du1 * dv2 - dv1 * du2;
		if (determinant == 0.f) {
        		// Handle zero determinant for triangle partial derivative matrix
			CoordinateSystem(Vector(nn), &dpdu, &dpdv);
    		} else {
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
private:
	vfloat<4> origx, origy, origz;
	vfloat<4> edge1x, edge1y, edge1z;
	vfloat<4> edge2x, edge2y, edge2z;
};

/***************************************************/
QBVHAccel::QBVHAccel(const vector<boost::shared_ptr<Primitive> > &p,
	u_int mp, u_int fst, u_int sf) : fullSweepThreshold(fst),
	skipFactor(sf), maxPrimsPerLeaf(mp)
{
	// Refine all primitives
	vector<boost::shared_ptr<Primitive> > vPrims;
	const PrimitiveRefinementHints refineHints(false);
	for (u_int i = 0; i < p.size(); ++i) {
		if(p[i]->CanIntersect())
			vPrims.push_back(p[i]);
		else
			p[i]->Refine(vPrims, refineHints, p[i]);
	}

	// Initialize primitives for _QBVHAccel_
	nPrims = vPrims.size();

	// Temporary data for building
	u_int *primsIndexes = new u_int[nPrims + 3]; // For the case where
	// the last quad would begin at the last primitive
	// (or the second or third last primitive)

	// The number of nodes depends on the number of primitives,
	// and is bounded by 2 * nPrims - 1.
	// Even if there will normally have at least 4 primitives per leaf,
	// it is not always the case => continue to use the normal bounds.
	nNodes = 0;
	maxNodes = 1;
	for (u_int layer = ((nPrims + maxPrimsPerLeaf - 1) / maxPrimsPerLeaf + 3) / 4; layer > 1; layer = (layer + 3) / 4)
		maxNodes += layer;
	nodes = AllocAligned<QBVHNode>(maxNodes);
	for (u_int i = 0; i < maxNodes; ++i)
		nodes[i] = QBVHNode();

	// The arrays that will contain
	// - the bounding boxes for all triangles
	// - the centroids for all triangles	
	BBox *primsBboxes = new BBox[nPrims];
	Point *primsCentroids = new Point[nPrims];
	// The bouding volume of all the centroids
	BBox centroidsBbox;
	
	// Fill each base array
	for (u_int i = 0; i < nPrims; ++i) {
		// This array will be reorganized during construction. 
		primsIndexes[i] = i;

		// Compute the bounding box for the triangle
		primsBboxes[i] = vPrims[i]->WorldBound();
		primsBboxes[i].Expand(MachineEpsilon::E(primsBboxes[i]));
		primsCentroids[i] = (primsBboxes[i].pMin +
			primsBboxes[i].pMax) * .5f;

		// Update the global bounding boxes
		worldBound = Union(worldBound, primsBboxes[i]);
		centroidsBbox = Union(centroidsBbox, primsCentroids[i]);
	}

	// Arbitrarily take the last primitive for the last 3
	primsIndexes[nPrims] = nPrims - 1;
	primsIndexes[nPrims + 1] = nPrims - 1;
	primsIndexes[nPrims + 2] = nPrims - 1;

	// Recursively build the tree
	LOG(LUX_DEBUG,LUX_NOERROR) << "Building QBVH, primitives: " << nPrims << ", initial nodes: " << maxNodes;
	nQuads = 0;
	BuildTree(0, nPrims, primsIndexes, primsBboxes, primsCentroids,
		worldBound, centroidsBbox, -1, 0, 0);

	prims = AllocAligned<boost::shared_ptr<QuadPrimitive> >(nQuads);
	nQuads = 0;
	PreSwizzle(0, primsIndexes, vPrims);
	LOG(LUX_DEBUG,LUX_NOERROR) << "QBVH completed with " << nNodes << "/" << maxNodes << " nodes";
	
	// Collect statistics
	maxDepth = 0;
	nodeCount = 0;
	noEmptyLeafCount = 0;
	emptyLeafCount = 0;
	primReferences = 0;
	SAHCost = CollectStatistics(0, 0, worldBound);
	avgLeafPrimReferences = primReferences / (noEmptyLeafCount > 0 ? noEmptyLeafCount : 1);
	
	// Print the statistics
	LOG(LUX_DEBUG, LUX_NOERROR) << "QBVH SAH total cost: " << SAHCost;
	LOG(LUX_DEBUG, LUX_NOERROR) << "QBVH max. depth: " << maxDepth;
	LOG(LUX_DEBUG, LUX_NOERROR) << "QBVH node count: " << nodeCount;
	LOG(LUX_DEBUG, LUX_NOERROR) << "QBVH empty leaf count: " << emptyLeafCount;
	LOG(LUX_DEBUG, LUX_NOERROR) << "QBVH not empty leaf count: " << noEmptyLeafCount;
	LOG(LUX_DEBUG, LUX_NOERROR) << "QBVH avg. primitive references per leaf: " << avgLeafPrimReferences;
	LOG(LUX_DEBUG, LUX_NOERROR) << "QBVH primitive references: " << primReferences << "/" << nPrims;
	
	// Release temporary memory
	delete[] primsBboxes;
	delete[] primsCentroids;
	delete[] primsIndexes;
}

float QBVHAccel::CollectStatistics(const int32_t nodeIndex, const u_int depth,
	const BBox &nodeBBox)
{
	maxDepth = max(maxDepth, depth);
	++nodeCount;

	const QBVHNode &node = nodes[nodeIndex];

	float cost = 1.f; // 1.f => Ct, the cost of traversing a node
	const float nodeSA = nodeBBox.SurfaceArea();
	for (int i = 0; i < 4; ++i) {
		BBox childBBox;
		childBBox = node.GetBBox(i);

		if (node.ChildIsLeaf(i)) {
			if (node.LeafIsEmpty(i))
				++emptyLeafCount;
			else {
				++noEmptyLeafCount;
				const u_int nPrims = node.NbPrimitivesInLeaf(i);
				primReferences += nPrims;
				// The classic SAH (Surface Area Heuristic)
				cost += (childBBox.SurfaceArea() / nodeSA) * nPrims; // * 1.f => Ci, the cost of intersecting
			}
		} else {
			// The probability to intersect the child node multiplied for the cost
			// of traveling the child node
			cost += childBBox.SurfaceArea() / nodeSA *
				CollectStatistics(node.children[i], depth + 1, childBBox);
		}
	}

	return cost;
}

/***************************************************/
void QBVHAccel::BuildTree(u_int start, u_int end, u_int *primsIndexes,
	const BBox *primsBboxes, const Point *primsCentroids, const BBox &nodeBbox,
	const BBox &centroidsBbox, int32_t parentIndex, int32_t childIndex, int depth)
{
	// Create a leaf ?
	//********
	if (depth > 64 || end - start <= maxPrimsPerLeaf) {
		if (depth > 64) {
			LOG(LUX_WARNING, LUX_LIMIT) << "Maximum recursion depth reached while constructing QBVH, forcing a leaf node";
			if (end - start > 64) {
				LOG(LUX_ERROR, LUX_LIMIT) << "QBVH unable to handle geometry, too many primitives in leaf";
				end = start + 64;
			}
		}
		CreateTempLeaf(parentIndex, childIndex, start, end, nodeBbox);
		return;
	}

	// Look for the split position
	int axis;
	float splitPos = BuildObjectSplit(start, end, primsIndexes, primsBboxes,
		primsCentroids, centroidsBbox, axis);
	
	if (isnan(splitPos)) {
		if (end - start > 64) {
			LOG(LUX_ERROR, LUX_LIMIT) << "QBVH unable to handle geometry, too many primitives with the same centroid";
			end = start + 64;
		}
		CreateTempLeaf(parentIndex, childIndex, start, end, nodeBbox);
		return;
	}

	BBox leftChildBbox, rightChildBbox;
	BBox leftChildCentroidsBbox, rightChildCentroidsBbox;

	u_int storeIndex = start;
	for (u_int i = start; i < end; ++i) {
		const u_int primIndex = primsIndexes[i];

		// This test isn't really correct because produces different results from
		// the one in BuildObjectSplit(). For instance, it happens when the centroid
		// is exactly on the split. SQBVH uses the right approach. However, this
		// kind of problem has no side effects in a pure QBVH so it is not worth
		// fixing here.
		if (primsCentroids[primIndex][axis] <= splitPos) {
			// Swap
			primsIndexes[i] = primsIndexes[storeIndex];
			primsIndexes[storeIndex] = primIndex;
			++storeIndex;
			
			// Update the bounding boxes,
			// this triangle is on the left side
			leftChildBbox = Union(leftChildBbox, primsBboxes[primIndex]);
			leftChildCentroidsBbox = Union(leftChildCentroidsBbox, primsCentroids[primIndex]);
		} else {
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
	if (depth % 2 == 0) {
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
}

float QBVHAccel::BuildObjectSplit(const u_int start, const u_int end,
	const u_int *primsIndexes, const BBox *primsBboxes,
	const Point *primsCentroids, const BBox &centroidsBbox, int &axis)
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
	if (isinf(k1))
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

	for (int i = 0; i < OBJECT_SPLIT_BINS; ++i)
		bins[i] = 0.f;

	u_int step = (end - start < fullSweepThreshold) ? 1 : skipFactor;

	for (u_int i = start; i < end; i += step) {
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

	for (int i = 0; i < OBJECT_SPLIT_BINS; ++i) {
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
	for (int i = 0; i < OBJECT_SPLIT_BINS - 1; ++i) {
		float cost = areaLeft[i] * QuadCount(nbPrimsLeft[i]) +
			areaRight[i + 1] * QuadCount(nbPrimsRight[i + 1]);
		if (cost < minCost) {
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
}

/***************************************************/
void QBVHAccel::CreateTempLeaf(int32_t parentIndex, int32_t childIndex,
	u_int start, u_int end, const BBox &nodeBbox)
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
	
	QBVHNode &node = nodes[parentIndex];

	node.SetBBox(childIndex, nodeBbox);

	u_int quads = QuadCount(nbPrimsTotal);
	
	// Use the same encoding as the final one, but with a different meaning.
	node.InitializeLeaf(childIndex, quads, start);

	nQuads += quads;
}

void QBVHAccel::PreSwizzle(int32_t nodeIndex, const u_int *primsIndexes,
	const vector<boost::shared_ptr<Primitive> > &vPrims)
{
	for (int i = 0; i < 4; ++i) {
		if (nodes[nodeIndex].ChildIsLeaf(i))
			CreateSwizzledLeaf(nodeIndex, i, primsIndexes, vPrims);
		else
			PreSwizzle(nodes[nodeIndex].children[i], primsIndexes, vPrims);
	}
}

void QBVHAccel::CreateSwizzledLeaf(int32_t parentIndex, int32_t childIndex,
	const u_int *primsIndexes, const vector<boost::shared_ptr<Primitive> > &vPrims)
{
	QBVHNode &node = nodes[parentIndex];
	if (node.LeafIsEmpty(childIndex))
		return;
	const u_int startQuad = nQuads;
	const u_int nbQuads = node.NbQuadsInLeaf(childIndex);

	u_int primOffset = node.FirstQuadIndexForLeaf(childIndex);
	u_int primNum = nQuads;

	for (u_int q = 0; q < nbQuads; ++q) {
		bool allTri = true;
		for (u_int i = 0; i < 4; ++i)
			allTri &= dynamic_cast<MeshBaryTriangle *>(vPrims[primsIndexes[primOffset + i]].get()) != NULL;
		if (allTri) {
			boost::shared_ptr<QuadPrimitive> p(new QuadTriangle(vPrims[primsIndexes[primOffset]], vPrims[primsIndexes[primOffset + 1]], vPrims[primsIndexes[primOffset + 2]], vPrims[primsIndexes[primOffset + 3]]));
			new (&prims[primNum]) boost::shared_ptr<QuadPrimitive>(p);
		} else {
			boost::shared_ptr<QuadPrimitive> p(new QuadPrimitive(vPrims[primsIndexes[primOffset]], vPrims[primsIndexes[primOffset + 1]], vPrims[primsIndexes[primOffset + 2]], vPrims[primsIndexes[primOffset + 3]]));
			new (&prims[primNum]) boost::shared_ptr<QuadPrimitive>(p);
		}
		++primNum;
		primOffset += 4;
	}
	nQuads += nbQuads;
	node.InitializeLeaf(childIndex, nbQuads, startQuad);
}

int32_t QBVHNode::BBoxIntersect(const QuadRay &ray4, const vfloat<4> invDir[3],
	const int sign[3]) const
{
	vfloat<4> tMin = ray4.mint;
	vfloat<4> tMax = ray4.maxt;

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
	return _mm_movemask_ps(_mm_cmpge_ps(tMax.as<__m128>(),tMin.as<__m128>()));
}

/***************************************************/
bool QBVHAccel::Intersect(const Ray &ray, Intersection *isect) const
{
	//------------------------------
	// Prepare the ray for intersection
	QuadRay ray4(ray);
	vfloat<4> invDir[3];
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
	
	while (todoNode >= 0) {
		// Leaves are identified by a negative index
		if (!QBVHNode::IsLeaf(nodeStack[todoNode])) {
			QBVHNode &node = nodes[nodeStack[todoNode]];
			--todoNode;
			
			const int32_t visit = node.BBoxIntersect(ray4, invDir,
				signs);

			if (visit & 0x1)
				nodeStack[++todoNode] = node.children[0];
			if (visit & 0x2)
				nodeStack[++todoNode] = node.children[1];
			if (visit & 0x4)
				nodeStack[++todoNode] = node.children[2];
			if (visit & 0x8)
				nodeStack[++todoNode] = node.children[3];
		} else {
			//----------------------
			// It is a leaf,
			// all the informations are encoded in the index
			const int32_t leafData = nodeStack[todoNode];
			--todoNode;
			
			if (QBVHNode::IsEmpty(leafData))
				continue;

			// Perform intersection
			const u_int nbQuadPrimitives = QBVHNode::NbQuadPrimitives(leafData);
			
			const u_int offset = QBVHNode::FirstQuadIndex(leafData);

			for (u_int primNumber = offset; primNumber < (offset + nbQuadPrimitives); ++primNumber)
				hit |= prims[primNumber]->Intersect(ray4, ray, isect);
		}//end of the else
	}

	return hit;
}

/***************************************************/
bool QBVHAccel::IntersectP(const Ray &ray) const
{
	//------------------------------
	// Prepare the ray for intersection
	QuadRay ray4(ray);
	vfloat<4> invDir[3];
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

	while (todoNode >= 0) {
		// Leaves are identified by a negative index
		if (!QBVHNode::IsLeaf(nodeStack[todoNode])) {
			QBVHNode &node = nodes[nodeStack[todoNode]];
			--todoNode;

			const int32_t visit = node.BBoxIntersect(ray4, invDir,
				signs);

			if (visit & 0x1)
				nodeStack[++todoNode] = node.children[0];
			if (visit & 0x2)
				nodeStack[++todoNode] = node.children[1];
			if (visit & 0x4)
				nodeStack[++todoNode] = node.children[2];
			if (visit & 0x8)
				nodeStack[++todoNode] = node.children[3];
		} else {
			//----------------------
			// It is a leaf,
			// all the informations are encoded in the index
			const int32_t leafData = nodeStack[todoNode];
			--todoNode;
			
			if (QBVHNode::IsEmpty(leafData))
				continue;

			// Perform intersection
			const u_int nbQuadPrimitives = QBVHNode::NbQuadPrimitives(leafData);
			
			const u_int offset = QBVHNode::FirstQuadIndex(leafData);

			for (u_int primNumber = offset; primNumber < (offset + nbQuadPrimitives); ++primNumber) {
				if (prims[primNumber]->IntersectP(ray))
					return true;
			}
		} // end of the else
	}

	return false;
}

/***************************************************/
QBVHAccel::~QBVHAccel()
{
	for (u_int i = 0; i < nQuads; ++i)
		prims[i].~shared_ptr();
	FreeAligned(prims);
	FreeAligned(nodes);
}

/***************************************************/
BBox QBVHAccel::WorldBound() const
{
	return worldBound;
}

void QBVHAccel::GetPrimitives(vector<boost::shared_ptr<Primitive> > &primitives) const
{
	primitives.reserve(primitives.size() + nPrims);
	for(u_int i = 0; i < nPrims; ++i)
		primitives.push_back(prims[i]);
	for (u_int i = 0; i < nPrims; ++i)
		prims[i]->GetPrimitives(primitives);
}

Aggregate* QBVHAccel::CreateAccelerator(const vector<boost::shared_ptr<Primitive> > &prims, const ParamSet &ps)
{
	int maxPrimsPerLeaf = ps.FindOneInt("maxprimsperleaf", 4);
	int fullSweepThreshold = ps.FindOneInt("fullsweepthreshold", 4 * maxPrimsPerLeaf);
	int skipFactor = ps.FindOneInt("skipfactor", 1);
	return new QBVHAccel(prims, maxPrimsPerLeaf, fullSweepThreshold, skipFactor);

}

static DynamicLoader::RegisterAccelerator<QBVHAccel> r("qbvh");

}
