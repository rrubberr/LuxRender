#pragma once

#include "lux.h"
#include "primitive.h"
#include "embree4/rtcore.h"
#include "embree4/rtcore_common.h"
#include "shapes/mesh.h"

inline void errorFunction(void* userPtr, enum RTCError error, const char* str)
{
	printf("error %d: %s\n", error, str);
};

namespace lux
{
	class embree_accel : public Aggregate
	{
		public:
		embree_accel(
			const vector<boost::shared_ptr<Primitive>> &p,
			int csamples, int icost, int tcost, float ebonus, int maxleafp = 8
		)
		{
			vector<boost::shared_ptr<Primitive>> vPrims = {};
			const PrimitiveRefinementHints refineHints(false);
			for(u_int i = 0; i < p.size(); ++i)
			{
				if(p[i]->CanIntersect())
					vPrims.push_back(p[i]);
				else
					p[i]->Refine(vPrims, refineHints, p[i]);
			}

			size_t tri_count = vPrims.size();
			u_int nPrims = vPrims.size();
			primitives = vPrims;

			for(u_int i = 0; i < nPrims; ++i)
				worldBound = Union(worldBound, vPrims[i]->WorldBound());

			m_dev = rtcNewDevice(nullptr);
			rtcSetDeviceErrorFunction(m_dev, errorFunction, NULL);

			m_scene = rtcNewScene(m_dev);
			m_geo = rtcNewGeometry(m_dev,RTC_GEOMETRY_TYPE_TRIANGLE);
			m_verts = (float*)rtcSetNewGeometryBuffer(
				m_geo,RTC_BUFFER_TYPE_VERTEX,0,
				RTC_FORMAT_FLOAT3,sizeof(float)*3,tri_count*3
			);
			m_indices = (uint32_t*)rtcSetNewGeometryBuffer(
				m_geo,RTC_BUFFER_TYPE_INDEX,0,
				RTC_FORMAT_UINT3,sizeof(uint32_t)*3,tri_count
			);

			size_t p_index = 0;
			size_t i_index = 0;
			for(size_t i = 0; i < vPrims.size() - 1; i++)
			{
				const MeshBaryTriangle* t = static_cast<const MeshBaryTriangle*>(vPrims[i].get());
				for(size_t j = 0; j < 3; j++)
				{
					m_verts[p_index+0] = t->GetP(j).x;
					m_verts[p_index+1] = t->GetP(j).y;
					m_verts[p_index+2] = t->GetP(j).z;
					p_index += 3;
				}
				m_indices[i_index+0] = i_index + 0;
				m_indices[i_index+1] = i_index + 1;
				m_indices[i_index+2] = i_index + 2;
				/*m_indices[i_index+0] = t->v[0];
				m_indices[i_index+1] = t->v[1];
				m_indices[i_index+2] = t->v[2];*/
				i_index += 3;
			}

			//memcpy(m_verts,m_tri_data->pos,sizeof(float)*3*3*tri_count);
			//memcpy(m_indices,m_tri_data->vin,sizeof(uint32_t)*3*tri_count);

			rtcCommitGeometry(m_geo);
			rtcAttachGeometry(m_scene,m_geo);
			rtcReleaseGeometry(m_geo);
			rtcCommitScene(m_scene);
		};
		
		~embree_accel()
		{

		};

		RTCRay fill_rtc_ray(const Ray &ray) const
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
		};
		
		BBox WorldBound() const
		{
			return worldBound;
		};
		
		bool CanIntersect() const {return true;};
		
		bool Intersect(const Ray &ray, Intersection *isect) const
		{
			struct RTCRayHit hit;
			hit.ray = fill_rtc_ray(ray);
			hit.hit.geomID = RTC_INVALID_GEOMETRY_ID;
			hit.hit.instID[0] = RTC_INVALID_GEOMETRY_ID;
			rtcIntersect1(m_scene,&hit);

			if(hit.hit.geomID == RTC_INVALID_GEOMETRY_ID)
				return false;

			const int32_t ti = m_indices[hit.hit.primID];
			
			const MeshBaryTriangle *triangle(static_cast<const MeshBaryTriangle *>(primitives[
				ti
			].get()));

			const Point p0 = triangle->GetP(0);
			const Point p1 = triangle->GetP(1);
			const Point p2 = triangle->GetP(2);

			const float b1 = hit.hit.u;
			const float b2 = hit.hit.v;
			const float b0 = 1.0f - b1 - b2;

			const Point o = p0;

			const Vector e1 = p1 - p0;
			const Vector e2 = p2 - p0;
			const float _b0 = b0;
			const float _b1 = b1;
			const float _b2 = b2;
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
		};
		
		bool IntersectP(const Ray &ray) const
		{
			struct RTCRayHit hit;
			hit.ray = fill_rtc_ray(ray);
			hit.hit.geomID = RTC_INVALID_GEOMETRY_ID;
			hit.hit.instID[0] = RTC_INVALID_GEOMETRY_ID;
			rtcIntersect1(m_scene,&hit);

			if(hit.hit.geomID == RTC_INVALID_GEOMETRY_ID)
				return false;
		};
		
		Transform GetLocalToWorld(float time) const {
			return Transform();
		};

		void GetPrimitives(vector<boost::shared_ptr<Primitive>> &prims) const
		{
			prims.reserve(primitives.size());
			for(u_int i = 0; i < primitives.size(); ++i)
				prims.push_back(primitives[i]);
		};

		static Aggregate *CreateAccelerator(
			const vector<boost::shared_ptr<Primitive>> &prims, const ParamSet &ps
		)
		{
			//none of these settings do anything
			int costSamples  = ps.FindOneInt("costsamples", 8);
			int isectCost    = ps.FindOneInt("intersectcost", 80);
			int travCost     = ps.FindOneInt("traversalcost", 1);
			float emptyBonus = ps.FindOneFloat("emptybonus", 0.0f);
			int maxLeafPrims = ps.FindOneInt("maxleafprims", 1);
			return new embree_accel(prims, costSamples, isectCost, travCost, emptyBonus, maxLeafPrims);
		};

		protected:
		vector<boost::shared_ptr<Primitive>> primitives = {};
		float* m_verts = nullptr;
		uint32_t* m_indices = nullptr;
		RTCScene m_scene  = 0;
		RTCDevice m_dev   = 0;
		RTCGeometry m_geo = 0;
		BBox worldBound;
	};
};