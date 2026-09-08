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
#include <atomic>
#include <map>

#include "error.h"
#include "embree.h"
#include "dynload.h"
#include "shapes/mesh.h"
#include "geometry/matrix3x3.h"

using namespace luxrays;
using namespace lux;

void lux::errorFunction(void* userPtr, enum RTCError error, const char* str)
{
	printf("error %d: %s\n", error, str);
}

void embree_accel::CollectLeafInfos(const boost::shared_ptr<Primitive> &prim,
	const AreaLight *areaLight, const MotionPrimitive *motion,
	vector<EmbreeLeafInfo> &out, u_int &skippedCount)
{
	const Primitive *raw = prim.get();

	if (const MeshBaryTriangle *tri = dynamic_cast<const MeshBaryTriangle *>(raw)) {
		// Retain this leaf's shared_ptr. If it was produced by the
		// Refine() call below, nothing else may be holding a reference,
		// and we need the pointer to stay valid.
		primitives.push_back(prim);

		EmbreeLeafInfo info;
		info.triangle = tri;
		info.areaLight = areaLight;
		info.motion = motion;
		out.push_back(info);
		return;
	}

	if (const MeshQuadrilateral *quad = dynamic_cast<const MeshQuadrilateral *>(raw)) {
		
		/*
		if (quad->isDegenerate()) {
			// Mesh::Refine() should already discard degenerate quads.
			// Fallback test in case edge cases emerge!
			static std::atomic<u_int> loggedDegenerateQuadCount(0);
			static const u_int kMaxDegenerateQuadMessages = 20;
			const u_int count = ++loggedDegenerateQuadCount;
			if (count <= kMaxDegenerateQuadMessages) {
				LOG(LUX_WARNING, LUX_LIMIT) << "embree_accel: skipping "
					<< "degenerate quadrilateral";
				if (count == kMaxDegenerateQuadMessages) {
					LOG(LUX_WARNING, LUX_LIMIT) << "embree_accel: further "
						<< "degenerate quadrilateral messages will be "
						<< "suppressed for the remainder of this render.";
				}
			}
			++skippedCount;
			return;
		}
		*/

		primitives.push_back(prim);

		EmbreeLeafInfo info;
		info.quad = quad;
		info.areaLight = areaLight;
		info.motion = motion;
		out.push_back(info);
		return;
	}

	if (const AreaLightPrimitive *alp = dynamic_cast<const AreaLightPrimitive *>(raw)) {
		// A primitive shouldn't be wrapped by more than one
		// AreaLightPrimitive; if it is, keep the outermost.
		const AreaLight *al = areaLight ? areaLight : alp->GetAreaLight().get();
		CollectLeafInfos(alp->GetPrimitive(), al, motion, out, skippedCount);
		return;
	}

	if (const MotionPrimitive *mp = dynamic_cast<const MotionPrimitive *>(raw)) {
		if (motion) {
			// Nested MotionPrimitives aren't supported.
			static std::atomic<u_int> loggedNestedMotionCount(0);
			static const u_int kMaxNestedMotionMessages = 20;
			const u_int count = ++loggedNestedMotionCount;
			if (count <= kMaxNestedMotionMessages) {
				LOG(LUX_WARNING, LUX_LIMIT) << "embree_accel: skipping "
					<< "nested MotionPrimitive (typeid: "
					<< typeid(*raw).name() << ")";
				if (count == kMaxNestedMotionMessages) {
					LOG(LUX_WARNING, LUX_LIMIT) << "embree_accel: further "
						<< "nested MotionPrimitive warnings will be "
						<< "suppressed.";
				}
			}
			++skippedCount;
			return;
		}
		CollectLeafInfos(mp->GetInstance(), areaLight, mp, out, skippedCount);
		return;
	}

	if (const Aggregate *agg = dynamic_cast<const Aggregate *>(raw)) {
		// Aggregates report CanIntersect() == true unconditionally;
		// expand into its constituent primitives.
		vector<boost::shared_ptr<Primitive>> children;
		agg->GetPrimitives(children);
		for (size_t i = 0; i < children.size(); ++i)
			CollectLeafInfos(children[i], areaLight, motion, out, skippedCount);
		return;
	}

	const Shape *asShape = dynamic_cast<const Shape *>(prim.get());
	if (!prim->CanIntersect() || (asShape && asShape->CanRefine())) {
		// Not yet in an intersectable form! Refine it and recurse
		// into whatever comes out...
		vector<boost::shared_ptr<Primitive>> refined;
		const PrimitiveRefinementHints refineHints(false);
		prim->Refine(refined, refineHints, prim);
		if (refined.empty()) {
			// A scene with many broken primitives could
			// flood the log.
			static std::atomic<u_int> loggedEmptyRefineCount(0);
			static const u_int kMaxEmptyRefineMessages = 20;
			const u_int count = ++loggedEmptyRefineCount;
			if (count <= kMaxEmptyRefineMessages) {
				LOG(LUX_WARNING, LUX_LIMIT) << "embree_accel: skipping "
					<< "primitive that produced no refined geometry "
					<< "(typeid: " << typeid(*raw).name() << ")";
				if (count == kMaxEmptyRefineMessages) {
					LOG(LUX_WARNING, LUX_LIMIT) << "embree_accel: further "
						<< "geometry warnings will be suppressed";
				}
			}
			++skippedCount;
			return;
		}
		for (size_t i = 0; i < refined.size(); ++i)
			CollectLeafInfos(refined[i], areaLight, motion, out, skippedCount);
		return;
	}

	// A genuinely unsupported leaf shape (e.g. evaluative geometry).
	static std::atomic<u_int> loggedNonTriangleCount(0);
	static const u_int kMaxNonTriangleMessages = 20;
	const u_int count = ++loggedNonTriangleCount;
	// Again, could flood the log.
	if (count <= kMaxNonTriangleMessages) {
		LOG(LUX_WARNING, LUX_LIMIT) << "embree_accel: skipping unsupported "
			<< "primitive (typeid: " << typeid(*raw).name() << ")";
		if (count == kMaxNonTriangleMessages) {
			LOG(LUX_WARNING, LUX_LIMIT) << "embree_accel: further "
				<< "unsupported shape warnings will be suppressed.";
		}
	}
	++skippedCount;
}

void embree_accel::BuildGeometryBuckets(
	const vector<vector<EmbreeLeafInfo>> &buckets,
	RTCGeometryType geomType, u_int vertsPerLeaf,
	u_int &motionGeomCount)
{
	for (size_t b = 0; b < buckets.size(); ++b) {
		const vector<EmbreeLeafInfo> &leafInfos = buckets[b];
		if (leafInfos.empty())
			continue;

		const bool isMotionBucket = (b != 0);
		const size_t leafCount = leafInfos.size();

		RTCGeometry geom = rtcNewGeometry(m_dev, geomType);

		uint32_t *indices = (uint32_t*)rtcSetNewGeometryBuffer(
			geom, RTC_BUFFER_TYPE_INDEX, 0,
			vertsPerLeaf == 4 ? RTC_FORMAT_UINT4 : RTC_FORMAT_UINT3,
			sizeof(uint32_t) * vertsPerLeaf, leafCount
		);
		for (size_t t = 0; t < leafCount; ++t)
			for (u_int j = 0; j < vertsPerLeaf; ++j)
				indices[t*vertsPerLeaf + j] = (uint32_t)(t*vertsPerLeaf + j);

		// Fetch vertex "j" of leaf "t" (whichever shape this bucket holds).
		auto getVert = [&](size_t t, u_int j) -> Point {
			const EmbreeLeafInfo &li = leafInfos[t];
			return li.quad ? li.quad->GetP(j) : li.triangle->GetP(j);
		};

		if (!isMotionBucket) {
			float *verts = (float*)rtcSetNewGeometryBuffer(
				geom, RTC_BUFFER_TYPE_VERTEX, 0,
				RTC_FORMAT_FLOAT3, sizeof(float)*3, leafCount*vertsPerLeaf
			);
			for (size_t t = 0; t < leafCount; ++t) {
				const EmbreeLeafInfo &li = leafInfos[t];

				// Static MotionPrimitives' constant transform will be
				// rejected by rtcSetGeometryTimeRange(). 
				
				// This bakes its fixed transform directly into the
				// vertex data and drops it into the static bucket.
				Transform fixedXform;
				const bool hasFixedXform = (li.motion != nullptr);
				if (hasFixedXform) {
					const MotionSystem &ms = li.motion->GetMotionSystem();
					fixedXform = Transform(ms.Sample(ms.StartTime()));
				}

				for (u_int j = 0; j < vertsPerLeaf; ++j) {
					Point pt = getVert(t, j);
					if (hasFixedXform)
						pt = fixedXform * pt;
					verts[(t*vertsPerLeaf+j)*3+0] = pt.x;
					verts[(t*vertsPerLeaf+j)*3+1] = pt.y;
					verts[(t*vertsPerLeaf+j)*3+2] = pt.z;
				}
			}
		} else {
			// Motion blur: sample the motion path at StartTime()/EndTime().
			// Let Embree interpolate vertex positions between them.
			const MotionPrimitive *motion = leafInfos[0].motion;
			const MotionSystem &ms = motion->GetMotionSystem();
			const float t0 = ms.StartTime();
			const float t1 = ms.EndTime();

			rtcSetGeometryTimeStepCount(geom, 2);
			rtcSetGeometryTimeRange(geom, t0, t1);

			const Transform xform0(ms.Sample(t0));
			const Transform xform1(ms.Sample(t1));

			for (int step = 0; step < 2; ++step) {
				const Transform &xform = (step == 0) ? xform0 : xform1;
				float *verts = (float*)rtcSetNewGeometryBuffer(
					geom, RTC_BUFFER_TYPE_VERTEX, step,
					RTC_FORMAT_FLOAT3, sizeof(float)*3, leafCount*vertsPerLeaf
				);
				for (size_t t = 0; t < leafCount; ++t) {
					for (u_int j = 0; j < vertsPerLeaf; ++j) {
						const Point pt = xform * getVert(t, j);
						verts[(t*vertsPerLeaf+j)*3+0] = pt.x;
						verts[(t*vertsPerLeaf+j)*3+1] = pt.y;
						verts[(t*vertsPerLeaf+j)*3+2] = pt.z;
					}
				}
			}
			++motionGeomCount;
		}

		rtcCommitGeometry(geom);
		unsigned int geomID = rtcAttachGeometry(m_scene, geom);
		rtcReleaseGeometry(geom);

		if (geomID >= m_leafInfo.size())
			m_leafInfo.resize(geomID + 1);
		m_leafInfo[geomID] = leafInfos;
	}
}

embree_accel::embree_accel(
	const vector<boost::shared_ptr<Primitive>> &p,
	bool highQuality, bool robust
)
{
	// Triangles and quads need separate RTCGeometry objects. Each
	// gets its own bucket. Bucket 0 holds every leaf of that kind
	// with no motion; each MotionPrimitive gets its own.
	primitives.clear();
	primitives.reserve(p.size());

	vector<vector<EmbreeLeafInfo>> triBuckets(1), quadBuckets(1);
	std::map<const MotionPrimitive *, size_t> triMotionBucketOf, quadMotionBucketOf;

	u_int skippedCount = 0;
	for (u_int i = 0; i < p.size(); ++i) {
		vector<EmbreeLeafInfo> collected;
		CollectLeafInfos(p[i], nullptr, nullptr, collected, skippedCount);

		if (collected.empty())
			continue;

		// Use the ORIGINAL primitive's WorldBound(). This 
		// captures MotionPrimitives' bounds correctly.
		worldBound = Union(worldBound, p[i]->WorldBound());

		for (size_t k = 0; k < collected.size(); ++k) {
			const EmbreeLeafInfo &info = collected[k];
			const bool isQuad = (info.quad != nullptr);

			vector<vector<EmbreeLeafInfo>> &buckets = isQuad ? quadBuckets : triBuckets;
			std::map<const MotionPrimitive *, size_t> &motionBucketOf =
				isQuad ? quadMotionBucketOf : triMotionBucketOf;

			// Route static MotionPrimitives into bucket 0.
			const bool isRealMotion = info.motion && !info.motion->GetMotionSystem().IsStatic();

			size_t bucket = 0;
			if (isRealMotion) {
				auto it = motionBucketOf.find(info.motion);
				if (it == motionBucketOf.end()) {
					bucket = buckets.size();
					buckets.push_back(vector<EmbreeLeafInfo>());
					motionBucketOf[info.motion] = bucket;
				} else {
					bucket = it->second;
				}
			}

			buckets[bucket].push_back(info);
		}
	}
	if (skippedCount > 0) {
		LOG(LUX_WARNING, LUX_LIMIT) << "embree_accel: " << skippedCount
			<< " primitive(s) skipped.";
	}

	m_dev = rtcNewDevice(nullptr);
	rtcSetDeviceErrorFunction(m_dev, errorFunction, NULL);

	m_scene = rtcNewScene(m_dev);
	m_leafInfo.clear();

	u_int motionGeomCount = 0;
	BuildGeometryBuckets(triBuckets, RTC_GEOMETRY_TYPE_TRIANGLE, 3, motionGeomCount);
	BuildGeometryBuckets(quadBuckets, RTC_GEOMETRY_TYPE_QUAD, 4, motionGeomCount);

	rtcSetSceneBuildQuality(m_scene,
		highQuality ? RTC_BUILD_QUALITY_HIGH : RTC_BUILD_QUALITY_MEDIUM);
	if(robust)
		rtcSetSceneFlags(m_scene, RTC_SCENE_FLAG_ROBUST);

	rtcCommitScene(m_scene);

	// The accelerator may be constructed several times; report only the first build.
	static std::atomic<bool> loggedConfig(false);
	if (!loggedConfig.exchange(true)) {
		LOG(LUX_INFO, LUX_NOERROR) << "Using Embree for ray intersection.";

		const bool robustConfirmed =
			(rtcGetSceneFlags(m_scene) & RTC_SCENE_FLAG_ROBUST) != 0;
		LOG(LUX_INFO, LUX_NOERROR) << "Using "
			<< (highQuality ? "HIGH" : "MEDIUM")
			<< " scene builder quality. Robust scene build "
			<< (robustConfirmed ? "ENABLED" : "DISABLED")
			<< ". " << motionGeomCount << " motion-blurred geometry group(s).";
	}
}

embree_accel::~embree_accel()
{
    if (m_scene)
        rtcReleaseScene(m_scene);
    if (m_dev)
        rtcReleaseDevice(m_dev);
}

RTCRay embree_accel::fill_rtc_ray(const Ray &ray) const
{
	struct RTCRay r;
	r.org_x = ray.o.x;
	r.org_y = ray.o.y;
	r.org_z = ray.o.z;
	r.dir_x = ray.d.x;
	r.dir_y = ray.d.y;
	r.dir_z = ray.d.z;
	r.tnear = ray.mint;
	r.tfar  = ray.maxt;
	r.time  = ray.time;
	r.mask  = -1;
	r.flags =  0;
	return r;
}

BBox embree_accel::WorldBound() const
{
	return worldBound;
}

bool embree_accel::CanIntersect() const
{
	return true;
}

// DifferentialGeometry for triangles.
DifferentialGeometry embree_accel::ComputeDifferentialGeometry(
	const MeshBaryTriangle *triangle, float b1, float b2,
	const Transform *motionXform) const
{
	const float b0 = 1.0f - b1 - b2;

	Point p0 = triangle->GetP(0);
	Point p1 = triangle->GetP(1);
	Point p2 = triangle->GetP(2);
	if (motionXform) {
		p0 = (*motionXform) * p0;
		p1 = (*motionXform) * p1;
		p2 = (*motionXform) * p2;
	}

	const Point o = p0;

	const Vector e1 = p1 - p0;
	const Vector e2 = p2 - p0;
	const Normal nn(Normalize(Cross(e1, e2)));
	const Point pp(o + b1 * e1 + b2 * e2);

	// Compute triangle partial derivatives
	Vector dpdu, dpdv;
	float uvs[3][2];
	triangle->GetUVs(uvs);

	// Compute deltas for triangle partial derivatives
	const float du1 = uvs[0][0] - uvs[2][0];
	const float du2 = uvs[1][0] - uvs[2][0];
	const float dv1 = uvs[0][1] - uvs[2][1];
	const float dv2 = uvs[1][1] - uvs[2][1];
	const Vector dp1(p0 - p2), dp2(p1 - p2);

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
	const float tu = b0 * uvs[0][0] + b1 * uvs[1][0] + b2 * uvs[2][0];
	const float tv = b0 * uvs[0][1] + b1 * uvs[1][1] + b2 * uvs[2][1];

	DifferentialGeometry dg(pp, nn, dpdu, dpdv,
		Normal(0, 0, 0), Normal(0, 0, 0), tu, tv, triangle);
	dg.iData.baryTriangle.coords[0] = b0;
	dg.iData.baryTriangle.coords[1] = b1;
	dg.iData.baryTriangle.coords[2] = b2;
	return dg;
}

// DifferentialGeometry for quadrilaterals.
DifferentialGeometry embree_accel::ComputeDifferentialGeometry(
	const MeshQuadrilateral *quad, float u, float v,
	const Transform *motionXform) const
{
	Point p00 = quad->GetP(0);
	Point p10 = quad->GetP(1);
	Point p11 = quad->GetP(2);
	Point p01 = quad->GetP(3);
	if (motionXform) {
		p00 = (*motionXform) * p00;
		p10 = (*motionXform) * p10;
		p11 = (*motionXform) * p11;
		p01 = (*motionXform) * p01;
	}

	// Bilinear position; Embree RTC_GEOMETRY_TYPE_QUAD hit coords.
	const float b00 = (1.f - u) * (1.f - v);
	const float b10 = u * (1.f - v);
	const float b11 = u * v;
	const float b01 = (1.f - u) * v;
	const Point pp = b00*p00 + b10*p10 + b11*p11 + b01*p01;

	const Vector e01 = p10 - p00;
	const Vector e02 = p11 - p00;
	const Vector e03 = p01 - p00;

	// Geometric normal.
	const Normal nn(Normalize(Cross(e01, e02)));

	// Partial derivatives.
	Vector dpdu, dpdv;
	float uv[4][2];
	quad->GetUVs(uv);

	float A[3][3], InvA[3][3];
	A[0][0] = uv[1][0] - uv[0][0];
	A[0][1] = uv[1][1] - uv[0][1];
	A[0][2] = uv[1][0] * uv[1][1] - uv[0][0] * uv[0][1];
	A[1][0] = uv[2][0] - uv[0][0];
	A[1][1] = uv[2][1] - uv[0][1];
	A[1][2] = uv[2][0] * uv[2][1] - uv[0][0] * uv[0][1];
	A[2][0] = uv[3][0] - uv[0][0];
	A[2][1] = uv[3][1] - uv[0][1];
	A[2][2] = uv[3][0] * uv[3][1] - uv[0][0] * uv[0][1];

	if (!Invert3x3(A, InvA)) {
		CoordinateSystem(Vector(nn), &dpdu, &dpdv);
	} else {
		dpdu = Vector(
			InvA[0][0] * e01.x + InvA[0][1] * e02.x + InvA[0][2] * e03.x,
			InvA[0][0] * e01.y + InvA[0][1] * e02.y + InvA[0][2] * e03.y,
			InvA[0][0] * e01.z + InvA[0][1] * e02.z + InvA[0][2] * e03.z);
		dpdv = Vector(
			InvA[1][0] * e01.x + InvA[1][1] * e02.x + InvA[1][2] * e03.x,
			InvA[1][0] * e01.y + InvA[1][1] * e02.y + InvA[1][2] * e03.y,
			InvA[1][0] * e01.z + InvA[1][1] * e02.z + InvA[1][2] * e03.z);
	}

	// u, v are the raw bilinear parameter; blend the per-vertex UVs
	// to get the actual texture coordinates.
	const float texU = b00*uv[0][0] + b10*uv[1][0] + b11*uv[2][0] + b01*uv[3][0];
	const float texV = b00*uv[0][1] + b10*uv[1][1] + b11*uv[2][1] + b01*uv[3][1];

	DifferentialGeometry dg(pp, nn, dpdu, dpdv,
		Normal(0, 0, 0), Normal(0, 0, 0), texU, texV, quad);
	// Keep the geometric parameter for normal/color/alpha
	// interpolation, which is a different "thing" from the
	// UV atlas coordinate in dg.u/dg.v.
	dg.iData.quadrilateral.coords[0] = u;
	dg.iData.quadrilateral.coords[1] = v;
	dg.AdjustNormal(quad->GetMesh()->reverseOrientation, quad->GetMesh()->transformSwapsHandedness);
	return dg;
}

bool embree_accel::Intersect(const Ray &ray, Intersection *isect) const
{
	struct RTCRayHit hit;
	hit.ray = fill_rtc_ray(ray);
	hit.hit.geomID = RTC_INVALID_GEOMETRY_ID;
	hit.hit.instID[0] = RTC_INVALID_GEOMETRY_ID;
	rtcIntersect1(m_scene, &hit);

	if(hit.hit.geomID == RTC_INVALID_GEOMETRY_ID)
		return false;

	ray.maxt = hit.ray.tfar;

	const EmbreeLeafInfo &info = m_leafInfo[hit.hit.geomID][hit.hit.primID];

	Transform motionXform;
	const Transform *motionXformPtr = nullptr;
	if (info.motion) {
		// Sample the MotionPrimitive transform for this ray time.
		motionXform = Transform(info.motion->GetMotionSystem().Sample(ray.time));
		motionXformPtr = &motionXform;
	}

	const Primitive *leafPrim;
	const Mesh *mesh;
	if (info.quad) {
		isect->dg = ComputeDifferentialGeometry(info.quad, hit.hit.u, hit.hit.v, motionXformPtr);
		leafPrim = info.quad;
		mesh = info.quad->GetMesh();
	} else {
		isect->dg = ComputeDifferentialGeometry(info.triangle, hit.hit.u, hit.hit.v, motionXformPtr);
		leafPrim = info.triangle;
		mesh = info.triangle->mesh;
	}

	const Transform objectToWorld = motionXformPtr
		? (*motionXformPtr) * mesh->ObjectToWorld
		: mesh->ObjectToWorld;

	// If this leaf came from a MotionPrimitive, report that as
	// the hit primitive; otherwise report the geometry directly.
	
	// Pass through AreaLights.
	isect->Set(objectToWorld,
		info.motion ? static_cast<const Primitive *>(info.motion) : leafPrim,
		mesh->GetMaterial(),
		mesh->GetExterior(),
		mesh->GetInterior(),
		info.areaLight);

	if (info.motion)
		isect->dg.handle = info.motion;

	return true;
}

bool embree_accel::IntersectP(const Ray &ray) const
{
	struct RTCRayHit hit;
	hit.ray = fill_rtc_ray(ray);
	hit.hit.geomID = RTC_INVALID_GEOMETRY_ID;
	hit.hit.instID[0] = RTC_INVALID_GEOMETRY_ID;
	rtcIntersect1(m_scene, &hit);

	if(hit.hit.geomID == RTC_INVALID_GEOMETRY_ID)
		return false;

	return true;
}

Transform embree_accel::GetLocalToWorld(float time) const
{
	return Transform();
}

void embree_accel::GetPrimitives(vector<boost::shared_ptr<Primitive>> &prims) const
{
	prims.reserve(primitives.size());
	for(u_int i = 0; i < primitives.size(); ++i)
		prims.push_back(primitives[i]);
}

Aggregate *embree_accel::CreateAccelerator(
	const vector<boost::shared_ptr<Primitive>> &prims, const ParamSet &ps
)
{
	// On by default; improves traversal speed.
	bool highQuality = ps.FindOneBool("highquality", true);
	// Off by default; degrades traversal speed. Mostly for edge cases.
	bool robust = ps.FindOneBool("robust", false);
	return new embree_accel(prims, highQuality, robust);
}

static lux::DynamicLoader::RegisterAccelerator<lux::embree_accel> r("embree");
