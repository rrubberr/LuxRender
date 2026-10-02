/***************************************************************************
 *   Copyright (C) 1998-2026 by authors (see AUTHORS.txt)                  *
 *                                                                         *
 *   This file is part of LuxRender.                                       *
 *                                                                         *
 *   LuxRender is free software; you can redistribute it and/or modify     *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   any later version.                                                    *
 *                                                                         *
 *   LuxRender is distributed in the hope that it will be useful,          *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the          *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program. If not, see <http://www.gnu.org/licenses/>   *
 *                                                                         *
 *   This project is based on PBRT; see <http://www.pbrt.org>              *
 ***************************************************************************/

#include "core/embree2.h"
#include "core/scene.h"
#include "core/shape.h"
#include "core/math.h"
#include "core/error.h"

#include <cstring>

namespace lux2
{

    // ---------------------------------------------------------------------------
    // RTCDevice
    // ---------------------------------------------------------------------------

    RTCDevice EmbreeScene::AcquireDevice()
    {
        static RTCDevice dev = []
        {
            RTCDevice d = rtcNewDevice("");
            rtcSetDeviceErrorFunction(
                d,
                [](void *, RTCError code, const char *str)
                {
                    LOG(LUX_SEVERE, LUX_BUG)
                        << "Embree error " << (int)code << ": " << str;
                },
                nullptr);
            return d;
        }();
        return dev;
    }

    EmbreeScene::~EmbreeScene()
    {
        if (m_scene)
            rtcReleaseScene(m_scene);
        // Device intentionally not released. Avoids
        // destruction order bugs.
    }

    // ---------------------------------------------------------------------------
    // Scene Build
    // ---------------------------------------------------------------------------

    void EmbreeScene::Build(SceneDescription &desc)
    {
        BuildFromMeshes(desc.meshes);
    }

    void EmbreeScene::BuildFromMeshes(const std::vector<MeshDesc> &meshes)
    {
        // Release any prior scene so rebuilds don't leak.
        if (m_scene)
        {
            rtcReleaseScene(m_scene);
            m_scene = nullptr;
        }
        m_bound = BBox();

        m_device = AcquireDevice();
        m_scene = rtcNewScene(m_device);
        // Default scene flags and build quality.

        // Single reserve pass.
        m_triCount = 0;
        for (const MeshDesc &m : meshes)
            m_triCount += m.tris.size();

        v0x.resize(m_triCount);
        v0y.resize(m_triCount);
        v0z.resize(m_triCount);
        v1x.resize(m_triCount);
        v1y.resize(m_triCount);
        v1z.resize(m_triCount);
        v2x.resize(m_triCount);
        v2y.resize(m_triCount);
        v2z.resize(m_triCount);
        n0x.resize(m_triCount);
        n0y.resize(m_triCount);
        n0z.resize(m_triCount);
        n1x.resize(m_triCount);
        n1y.resize(m_triCount);
        n1z.resize(m_triCount);
        n2x.resize(m_triCount);
        n2y.resize(m_triCount);
        n2z.resize(m_triCount);
        uv0u.resize(m_triCount);
        uv0v.resize(m_triCount);
        uv1u.resize(m_triCount);
        uv1v.resize(m_triCount);
        uv2u.resize(m_triCount);
        uv2v.resize(m_triCount);
        matID.resize(m_triCount);
        lightID.resize(m_triCount);

        geomToBase.assign(meshes.size(), 0u);
        std::uint32_t base = 0;

        for (size_t mi = 0; mi < meshes.size(); ++mi)
        {
            const MeshDesc &m = meshes[mi];
            const size_t n = m.tris.size();
            geomToBase[mi] = base;
            if (n == 0)
                continue;

            //  Append into tables.
            for (size_t i = 0; i < n; ++i)
            {
                const TriangleDesc &td = m.tris[i];
                const size_t g = base + i;
                v0x[g] = td.v0.x();
                v0y[g] = td.v0.y();
                v0z[g] = td.v0.z();
                v1x[g] = td.v1.x();
                v1y[g] = td.v1.y();
                v1z[g] = td.v1.z();
                v2x[g] = td.v2.x();
                v2y[g] = td.v2.y();
                v2z[g] = td.v2.z();
                n0x[g] = td.n0.x();
                n0y[g] = td.n0.y();
                n0z[g] = td.n0.z();
                n1x[g] = td.n1.x();
                n1y[g] = td.n1.y();
                n1z[g] = td.n1.z();
                n2x[g] = td.n2.x();
                n2y[g] = td.n2.y();
                n2z[g] = td.n2.z();
                uv0u[g] = td.uv0.u;
                uv0v[g] = td.uv0.v;
                uv1u[g] = td.uv1.u;
                uv1v[g] = td.uv1.v;
                uv2u[g] = td.uv2.u;
                uv2v[g] = td.uv2.v;
                matID[g] = td.matID;
                lightID[g] = td.lightID;
            }
            m_bound = Union(m_bound, m.bound);

            // Embree triangle geometry.
            RTCGeometry geom = rtcNewGeometry(m_device, RTC_GEOMETRY_TYPE_TRIANGLE);

            // Triangle i references verts[3i..3i+2], so the
            // vertex buffer holds 3n vertices.
            float *verts = static_cast<float *>(rtcSetNewGeometryBuffer(
                geom, RTC_BUFFER_TYPE_VERTEX, 0, RTC_FORMAT_FLOAT3,
                3 * sizeof(float), 3 * n));
            unsigned *tris = static_cast<unsigned *>(rtcSetNewGeometryBuffer(
                geom, RTC_BUFFER_TYPE_INDEX, 0, RTC_FORMAT_UINT3,
                3 * sizeof(unsigned), n));
            for (size_t i = 0; i < n; ++i)
            {
                const TriangleDesc &td = m.tris[i];
                // Each triangle = 3 vertices = 9 floats; 9*i so
                // consecutive triangles don't overlap. Identity indices refer
                // to vertex slots 3i, 3i+1, 3i+2.
                float *v = verts + 9 * i;
                v[0] = td.v0.x();
                v[1] = td.v0.y();
                v[2] = td.v0.z();
                v[3] = td.v1.x();
                v[4] = td.v1.y();
                v[5] = td.v1.z();
                v[6] = td.v2.x();
                v[7] = td.v2.y();
                v[8] = td.v2.z();
                tris[3 * i + 0] = static_cast<unsigned>(3 * i + 0);
                tris[3 * i + 1] = static_cast<unsigned>(3 * i + 1);
                tris[3 * i + 2] = static_cast<unsigned>(3 * i + 2);
            }
            rtcSetGeometryMask(geom, m.tris[0].groupMask);
            rtcSetGeometryBuildQuality(geom, RTC_BUILD_QUALITY_MEDIUM);
            rtcCommitGeometry(geom);
            // geomID == mesh index.
            rtcAttachGeometryByID(m_scene, geom, static_cast<unsigned>(mi));
            rtcReleaseGeometry(geom); // scene retains it
            base += static_cast<std::uint32_t>(n);
        }

        rtcCommitScene(m_scene);
    }

    // ---------------------------------------------------------------------------
    // Packet staging helpers
    // ---------------------------------------------------------------------------

    namespace
    {

        template <int W>
        void IntersectPacket(const RTCScene scene, RayP &ray, HitP &hit,
                             Coherent hint)
        {
            alignas(64) int valid[W];
            enoki::store(valid, enoki::select(ray.alive, Int32P(-1), Int32P(0)));

            RTCRayQueryContext ctx;
            rtcInitRayQueryContext(&ctx);
            RTCIntersectArguments args;
            rtcInitIntersectArguments(&args);
            args.context = &ctx;
            args.flags = (hint == Coherent::Yes) ? RTC_RAY_QUERY_FLAG_COHERENT
                                                 : RTC_RAY_QUERY_FLAG_INCOHERENT;

            FloatP t;
            UInt32P geomID, primID;
            FloatP b1, b2;
            Normal3fP ngeo;

            if constexpr (W == 1)
            {
                RTCRayHit rh{}; // miss lanes get well defined Ng/u/v
                enoki::store(&rh.ray.org_x, ray.o.x());
                enoki::store(&rh.ray.org_y, ray.o.y());
                enoki::store(&rh.ray.org_z, ray.o.z());
                enoki::store(&rh.ray.tnear, ray.mint);
                enoki::store(&rh.ray.dir_x, ray.d.x());
                enoki::store(&rh.ray.dir_y, ray.d.y());
                enoki::store(&rh.ray.dir_z, ray.d.z());
                enoki::store(&rh.ray.time, ray.time);
                enoki::store(&rh.ray.tfar, ray.maxt);
                enoki::store(&rh.ray.mask, ray.mask);
                rh.ray.id = 0;
                rh.ray.flags = 0;
                rh.hit.geomID = RTC_INVALID_GEOMETRY_ID;

                rtcIntersect1(scene, &rh, &args);

                t = enoki::load<FloatP>(&rh.ray.tfar);
                geomID = enoki::load<UInt32P>(&rh.hit.geomID);
                primID = enoki::load<UInt32P>(&rh.hit.primID);
                b1 = enoki::load<FloatP>(&rh.hit.u);
                b2 = enoki::load<FloatP>(&rh.hit.v);
                ngeo = Normal3fP(enoki::load<FloatP>(&rh.hit.Ng_x),
                                 enoki::load<FloatP>(&rh.hit.Ng_y),
                                 enoki::load<FloatP>(&rh.hit.Ng_z));
            }
            else
            {
                using RH = std::conditional_t<W == 4, RTCRayHit4,
                                              std::conditional_t<W == 8, RTCRayHit8, RTCRayHit16>>;
                RH rh{}; // miss lanes get well-defined Ng/u/v
                enoki::store(rh.ray.org_x, ray.o.x());
                enoki::store(rh.ray.org_y, ray.o.y());
                enoki::store(rh.ray.org_z, ray.o.z());
                enoki::store(rh.ray.tnear, ray.mint);
                enoki::store(rh.ray.dir_x, ray.d.x());
                enoki::store(rh.ray.dir_y, ray.d.y());
                enoki::store(rh.ray.dir_z, ray.d.z());
                enoki::store(rh.ray.time, ray.time);
                enoki::store(rh.ray.tfar, ray.maxt);
                enoki::store(rh.ray.mask, ray.mask);
                enoki::store(rh.ray.id, UInt32P(0));
                enoki::store(rh.ray.flags, UInt32P(0));
                for (size_t l = 0; l < W; ++l)
                    rh.hit.geomID[l] = RTC_INVALID_GEOMETRY_ID;

                if constexpr (W == 4)
                    rtcIntersect4(valid, scene, &rh, &args);
                else if constexpr (W == 8)
                    rtcIntersect8(valid, scene, &rh, &args);
                else
                    rtcIntersect16(valid, scene, &rh, &args);

                t = enoki::load<FloatP>(rh.ray.tfar);
                geomID = enoki::load<UInt32P>(rh.hit.geomID);
                primID = enoki::load<UInt32P>(rh.hit.primID);
                b1 = enoki::load<FloatP>(rh.hit.u);
                b2 = enoki::load<FloatP>(rh.hit.v);
                ngeo = Normal3fP(enoki::load<FloatP>(rh.hit.Ng_x),
                                 enoki::load<FloatP>(rh.hit.Ng_y),
                                 enoki::load<FloatP>(rh.hit.Ng_z));
            }

            // Miss lanes have Ng==0.
            const FloatP ng2 = ngeo.x() * ngeo.x() + ngeo.y() * ngeo.y() +
                               ngeo.z() * ngeo.z();
            const FloatP ngInv = select(ng2 > FloatP(0.f), rsqrt(ng2), FloatP(0.f));
            ngeo = Normal3fP(ngeo.x() * ngInv, ngeo.y() * ngInv, ngeo.z() * ngInv);

            // Hit via the primed geomID.
            hit.hit = ray.alive && neq(geomID, UInt32P(RTC_INVALID_GEOMETRY_ID));
            const FloatP newT = select(hit.hit, t, ray.maxt);
            // Dead lanes are left untouched.
            hit.t = select(ray.alive, newT, hit.t);
            hit.geomID = geomID;
            hit.primID = primID;
            hit.b1 = b1;
            hit.b2 = b2;
            hit.ngeo = ngeo;
            ray.maxt = select(ray.alive, newT, ray.maxt);
        }

        template <int W>
        MaskP OccludedPacket(const RTCScene scene, const RayP &ray, MaskP active,
                             Coherent hint)
        {
            alignas(64) int valid[W];
            enoki::store(valid, enoki::select(active, Int32P(-1), Int32P(0)));

            RTCRayQueryContext ctx;
            rtcInitRayQueryContext(&ctx);
            RTCOccludedArguments args;
            rtcInitOccludedArguments(&args);
            args.context = &ctx;
            args.flags = (hint == Coherent::Yes) ? RTC_RAY_QUERY_FLAG_COHERENT
                                                 : RTC_RAY_QUERY_FLAG_INCOHERENT;

            FloatP t;
            if constexpr (W == 1)
            {
                RTCRay r;
                enoki::store(&r.org_x, ray.o.x());
                enoki::store(&r.org_y, ray.o.y());
                enoki::store(&r.org_z, ray.o.z());
                enoki::store(&r.tnear, ray.mint);
                enoki::store(&r.dir_x, ray.d.x());
                enoki::store(&r.dir_y, ray.d.y());
                enoki::store(&r.dir_z, ray.d.z());
                enoki::store(&r.time, ray.time);
                enoki::store(&r.tfar, ray.maxt);
                enoki::store(&r.mask, ray.mask);
                r.id = 0;
                r.flags = 0;
                rtcOccluded1(scene, &r, &args);
                t = enoki::load<FloatP>(&r.tfar);
            }
            else
            {
                using R = std::conditional_t<W == 4, RTCRay4,
                                             std::conditional_t<W == 8, RTCRay8, RTCRay16>>;
                R r;
                enoki::store(r.org_x, ray.o.x());
                enoki::store(r.org_y, ray.o.y());
                enoki::store(r.org_z, ray.o.z());
                enoki::store(r.tnear, ray.mint);
                enoki::store(r.dir_x, ray.d.x());
                enoki::store(r.dir_y, ray.d.y());
                enoki::store(r.dir_z, ray.d.z());
                enoki::store(r.time, ray.time);
                enoki::store(r.tfar, ray.maxt);
                enoki::store(r.mask, ray.mask);
                enoki::store(r.id, UInt32P(0));
                enoki::store(r.flags, UInt32P(0));

                if constexpr (W == 4)
                    rtcOccluded4(valid, scene, &r, &args);
                else if constexpr (W == 8)
                    rtcOccluded8(valid, scene, &r, &args);
                else
                    rtcOccluded16(valid, scene, &r, &args);

                t = enoki::load<FloatP>(r.tfar);
            }

            // Occluded where tfar changed from maxt.
            return active && neq(t, ray.maxt);
        }

    } // namespace

    // ---------------------------------------------------------------------------
    // Intersect
    // ---------------------------------------------------------------------------

    void EmbreeScene::Intersect(RayP &ray, HitP &hit, Coherent hint) const
    {
        if (!m_scene || !any(ray.alive))
            return;

        IntersectPacket<PACKET_WIDTH>(m_scene, ray, hit, hint);
        ShadeHit(ray, hit);
    }

    // ---------------------------------------------------------------------------
    // Occluded
    // ---------------------------------------------------------------------------

    MaskP EmbreeScene::Occluded(const RayP &ray, MaskP active,
                                Coherent hint) const
    {
        if (!m_scene || !any(active))
            return MaskP(false);

        return OccludedPacket<PACKET_WIDTH>(m_scene, ray, active, hint);
    }

    // ---------------------------------------------------------------------------
    // ShadeHit
    // ---------------------------------------------------------------------------

    void EmbreeScene::ShadeHit(const RayP &ray, HitP &hit) const
    {
        const MaskP active = hit.hit;
        if (!any(active))
            return;

        // Global triangle index per lane. Enoki gather requires a signed index.
        const Int32P gidx =
            enoki::gather<Int32P>(geomToBase.data(),
                                  enoki::reinterpret_array<Int32P>(hit.geomID),
                                  active) +
            enoki::reinterpret_array<Int32P>(hit.primID);

        const FloatP b0 = FloatP(1.f) - hit.b1 - hit.b2;

        // Position: p = b0*v0 + b1*v1 + b2*v2.
        const FloatP v0x_ = enoki::gather<FloatP>(v0x.data(), gidx, active);
        const FloatP v0y_ = enoki::gather<FloatP>(v0y.data(), gidx, active);
        const FloatP v0z_ = enoki::gather<FloatP>(v0z.data(), gidx, active);
        const FloatP v1x_ = enoki::gather<FloatP>(v1x.data(), gidx, active);
        const FloatP v1y_ = enoki::gather<FloatP>(v1y.data(), gidx, active);
        const FloatP v1z_ = enoki::gather<FloatP>(v1z.data(), gidx, active);
        const FloatP v2x_ = enoki::gather<FloatP>(v2x.data(), gidx, active);
        const FloatP v2y_ = enoki::gather<FloatP>(v2y.data(), gidx, active);
        const FloatP v2z_ = enoki::gather<FloatP>(v2z.data(), gidx, active);
        hit.p = Point3fP(fmadd(b0, v0x_, fmadd(hit.b1, v1x_, hit.b2 * v2x_)),
                         fmadd(b0, v0y_, fmadd(hit.b1, v1y_, hit.b2 * v2y_)),
                         fmadd(b0, v0z_, fmadd(hit.b1, v1z_, hit.b2 * v2z_)));

        // Fall back to the geometric normal on degenerate interpolation to stay NaN-free.
        const FloatP n0x_ = enoki::gather<FloatP>(n0x.data(), gidx, active);
        const FloatP n0y_ = enoki::gather<FloatP>(n0y.data(), gidx, active);
        const FloatP n0z_ = enoki::gather<FloatP>(n0z.data(), gidx, active);
        const FloatP n1x_ = enoki::gather<FloatP>(n1x.data(), gidx, active);
        const FloatP n1y_ = enoki::gather<FloatP>(n1y.data(), gidx, active);
        const FloatP n1z_ = enoki::gather<FloatP>(n1z.data(), gidx, active);
        const FloatP n2x_ = enoki::gather<FloatP>(n2x.data(), gidx, active);
        const FloatP n2y_ = enoki::gather<FloatP>(n2y.data(), gidx, active);
        const FloatP n2z_ = enoki::gather<FloatP>(n2z.data(), gidx, active);
        const Normal3fP n(b0 * n0x_ + hit.b1 * n1x_ + hit.b2 * n2x_,
                          b0 * n0y_ + hit.b1 * n1y_ + hit.b2 * n2y_,
                          b0 * n0z_ + hit.b1 * n1z_ + hit.b2 * n2z_);
        const FloatP sn2 = squared_norm(n);
        const Normal3fP nhat(n * rsqrt(sn2));
        hit.sh_n = select(sn2 > FloatP(0.f), nhat, hit.ngeo);

        // UV interpolation.
        const FloatP u0_ = enoki::gather<FloatP>(uv0u.data(), gidx, active);
        const FloatP v0_ = enoki::gather<FloatP>(uv0v.data(), gidx, active);
        const FloatP u1_ = enoki::gather<FloatP>(uv1u.data(), gidx, active);
        const FloatP v1_ = enoki::gather<FloatP>(uv1v.data(), gidx, active);
        const FloatP u2_ = enoki::gather<FloatP>(uv2u.data(), gidx, active);
        const FloatP v2_ = enoki::gather<FloatP>(uv2v.data(), gidx, active);
        hit.uv = Point2fP(b0 * u0_ + hit.b1 * u1_ + hit.b2 * u2_,
                          b0 * v0_ + hit.b1 * v1_ + hit.b2 * v2_);

        // UV gradient solve for the shading frame.
        const Vector3fP dp1(v0x_ - v2x_, v0y_ - v2y_, v0z_ - v2z_);
        const Vector3fP dp2(v1x_ - v2x_, v1y_ - v2y_, v1z_ - v2z_);
        const FloatP du1 = u0_ - u2_, du2 = u1_ - u2_;
        const FloatP dv1 = v0_ - v2_, dv2 = v1_ - v2_;
        const FloatP det = du1 * dv2 - dv1 * du2;
        const MaskP nonDet = det != FloatP(0.f);
        const FloatP invdet = select(nonDet, FloatP(1.f) / det, FloatP(0.f));
        Vector3fP dpdu = (dp1 * dv2 - dp2 * dv1) * invdet;
        Vector3fP dpdv = (dp2 * du1 - dp1 * du2) * invdet;
        // Make sure degenerate UVs stay finite.
        const auto fallback = coordinate_system(Vector3fP(hit.ngeo.x(),
                                                          hit.ngeo.y(),
                                                          hit.ngeo.z()));
        enoki::masked(dpdu, !nonDet) = fallback.first;
        enoki::masked(dpdv, !nonDet) = fallback.second;
        hit.dp_du = dpdu;
        hit.dp_dv = dpdv;

        // BSDF anisotropy frame on the interpolated shading normal where
        // sn = normalize(dpdu projected off n), tn = cross(n, sn).
        const Vector3fP nvec(hit.sh_n.x(), hit.sh_n.y(), hit.sh_n.z());
        const Vector3fP proj = dpdu - nvec * dot(dpdu, nvec);
        const FloatP proj2 = dot(proj, proj);
        const auto nBasis = coordinate_system(nvec);
        Vector3fP sn = select(proj2 > EPS_RAY * EPS_RAY,
                              proj * rsqrt(max(proj2, FloatP(EPS_DENOM))),
                              nBasis.first);
        hit.dp_ds = sn;
        hit.dp_dt = enoki::cross(nvec, sn);

        hit.matID = enoki::gather<UInt32P>(matID.data(), gidx, active);
        hit.lightID = enoki::gather<Int32P>(lightID.data(), gidx, active);
    }

} // namespace lux2
