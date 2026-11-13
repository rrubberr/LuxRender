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

#include "core/accel/embree2.h"
#include "core/scene.h"
#include "core/shape.h"
#include "core/math/math.h"
#include "core/math/simd_transpose.h"
#include "core/error.h"

#include <cstring>

namespace lux2
{

    // The transposed ShadeHit kernel addresses record fields by word index.
    static_assert(TRI_WORDS == 32u, "triangle record is 32 words");
    static_assert(kTri_v0 == 0u && kTri_v1 == 3u && kTri_v2 == 6u,
                  "record order: v0,v1,v2");
    static_assert(kTri_n0 == 9u && kTri_n1 == 12u && kTri_n2 == 15u,
                  "record order: n0,n1,n2");
    static_assert(kTri_uv0 == 18u && kTri_uv1 == 20u && kTri_uv2 == 22u,
                  "record order: uv0,uv1,uv2");
    static_assert(kTri_matID == 24u && kTri_lightID == 25u &&
                      kTri_medFront == 26u && kTri_medBack == 27u,
                  "record order: ids");

    // ---------------------------------------------------------------------------
    // RTCDevice
    // ---------------------------------------------------------------------------

    RTCDevice EmbreeScene::AcquireDevice()
    {
        static RTCDevice dev = []
        {
#ifdef LUX_USE_HUGEPAGES
            // Ask Embree explicitly for hugepages.
            RTCDevice d = rtcNewDevice("hugepages=1");
#else
            RTCDevice d = rtcNewDevice("");
#endif
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
        // Device intentionally not released.
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
        // Release any prior scene so rebuilds do not leak.
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

        triRecords.assign(size_t(m_triCount) * TRI_WORDS, 0u);

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
                uint32_t *rec = triRecords.data() + g * TRI_WORDS;
                auto putF = [&](uint32_t w, float v)
                { std::memcpy(rec + w, &v, sizeof(v)); };
                auto putU = [&](uint32_t w, uint32_t v)
                { rec[w] = v; };
                putF(kTri_v0 + 0, td.v0.x());
                putF(kTri_v0 + 1, td.v0.y());
                putF(kTri_v0 + 2, td.v0.z());
                putF(kTri_v1 + 0, td.v1.x());
                putF(kTri_v1 + 1, td.v1.y());
                putF(kTri_v1 + 2, td.v1.z());
                putF(kTri_v2 + 0, td.v2.x());
                putF(kTri_v2 + 1, td.v2.y());
                putF(kTri_v2 + 2, td.v2.z());
                putF(kTri_n0 + 0, td.n0.x());
                putF(kTri_n0 + 1, td.n0.y());
                putF(kTri_n0 + 2, td.n0.z());
                putF(kTri_n1 + 0, td.n1.x());
                putF(kTri_n1 + 1, td.n1.y());
                putF(kTri_n1 + 2, td.n1.z());
                putF(kTri_n2 + 0, td.n2.x());
                putF(kTri_n2 + 1, td.n2.y());
                putF(kTri_n2 + 2, td.n2.z());
                putF(kTri_uv0 + 0, td.uv0.u);
                putF(kTri_uv0 + 1, td.uv0.v);
                putF(kTri_uv1 + 0, td.uv1.u);
                putF(kTri_uv1 + 1, td.uv1.v);
                putF(kTri_uv2 + 0, td.uv2.u);
                putF(kTri_uv2 + 1, td.uv2.v);
                putU(kTri_matID, td.matID);
                putU(kTri_lightID, uint32_t(td.lightID));
                putU(kTri_medFront, td.mediumFrontID);
                putU(kTri_medBack, td.mediumBackID);
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
            rtcSetGeometryBuildQuality(geom, RTC_BUILD_QUALITY_HIGH);
            rtcSetSceneFlags(m_scene, RTC_SCENE_FLAG_ROBUST);
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
            const FloatP ng2 = fmadd(ngeo.x(), ngeo.x(),
                                     fmadd(ngeo.y(), ngeo.y(),
                                           ngeo.z() * ngeo.z()));
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
        if (!m_scene || none(ray.alive))
            return;

        IntersectPacket<PACKET_WIDTH>(m_scene, ray, hit, hint);
        ShadeHit(hit);
    }

    // ---------------------------------------------------------------------------
    // Occluded
    // ---------------------------------------------------------------------------

    MaskP EmbreeScene::Occluded(const RayP &ray, MaskP active,
                                Coherent hint) const
    {
        if (!m_scene || none(active))
            return MaskP(false);

        return OccludedPacket<PACKET_WIDTH>(m_scene, ray, active, hint);
    }

    // ---------------------------------------------------------------------------
    // ShadeHit
    // ---------------------------------------------------------------------------

    namespace
    {
        // Shading math shared by the transposed and gather load path.
        template <typename GetF, typename GetU>
        void ShadeFromFields(HitP &hit, GetF F, GetU U)
        {
            const FloatP b0 = FloatP(1.f) - hit.b1 - hit.b2;

            // Position: p = b0*v0 + b1*v1 + b2*v2.
            const FloatP v0x_ = F(kTri_v0 + 0), v0y_ = F(kTri_v0 + 1),
                         v0z_ = F(kTri_v0 + 2);
            const FloatP v1x_ = F(kTri_v1 + 0), v1y_ = F(kTri_v1 + 1),
                         v1z_ = F(kTri_v1 + 2);
            const FloatP v2x_ = F(kTri_v2 + 0), v2y_ = F(kTri_v2 + 1),
                         v2z_ = F(kTri_v2 + 2);
            hit.p = Point3fP(fmadd(b0, v0x_, fmadd(hit.b1, v1x_, hit.b2 * v2x_)),
                             fmadd(b0, v0y_, fmadd(hit.b1, v1y_, hit.b2 * v2y_)),
                             fmadd(b0, v0z_, fmadd(hit.b1, v1z_, hit.b2 * v2z_)));

            // Fall back to the geometric normal on degenerate interpolation to stay NaN free.
            const FloatP n0x_ = F(kTri_n0 + 0), n0y_ = F(kTri_n0 + 1),
                         n0z_ = F(kTri_n0 + 2);
            const FloatP n1x_ = F(kTri_n1 + 0), n1y_ = F(kTri_n1 + 1),
                         n1z_ = F(kTri_n1 + 2);
            const FloatP n2x_ = F(kTri_n2 + 0), n2y_ = F(kTri_n2 + 1),
                         n2z_ = F(kTri_n2 + 2);
            const Normal3fP n(fmadd(b0, n0x_, fmadd(hit.b1, n1x_, hit.b2 * n2x_)),
                              fmadd(b0, n0y_, fmadd(hit.b1, n1y_, hit.b2 * n2y_)),
                              fmadd(b0, n0z_, fmadd(hit.b1, n1z_, hit.b2 * n2z_)));
            const FloatP sn2 = squared_norm(n);
            const Normal3fP nhat(n * rsqrt(sn2));
            hit.sh_n = select(sn2 > FloatP(0.f), nhat, hit.ngeo);

            // UV interpolation.
            const FloatP u0_ = F(kTri_uv0 + 0), v0_ = F(kTri_uv0 + 1);
            const FloatP u1_ = F(kTri_uv1 + 0), v1_ = F(kTri_uv1 + 1);
            const FloatP u2_ = F(kTri_uv2 + 0), v2_ = F(kTri_uv2 + 1);
            hit.uv = Point2fP(fmadd(b0, u0_, fmadd(hit.b1, u1_, hit.b2 * u2_)),
                              fmadd(b0, v0_, fmadd(hit.b1, v1_, hit.b2 * v2_)));

            // UV gradient solve for the shading frame.
            const Vector3fP dp1(v0x_ - v2x_, v0y_ - v2y_, v0z_ - v2z_);
            const Vector3fP dp2(v1x_ - v2x_, v1y_ - v2y_, v1z_ - v2z_);
            const FloatP du1 = u0_ - u2_, du2 = u1_ - u2_;
            const FloatP dv1 = v0_ - v2_, dv2 = v1_ - v2_;
            const FloatP det = du1 * dv2 - dv1 * du2;
            const MaskP nonDet = neq(det, FloatP(0.f));
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

            hit.matID = U(kTri_matID);
            hit.lightID = enoki::reinterpret_array<Int32P>(U(kTri_lightID));
            hit.mediumFrontID = U(kTri_medFront);
            hit.mediumBackID = U(kTri_medBack);
        }
    } // namespace

    void EmbreeScene::ShadeHit(HitP &hit) const
    {
        const MaskP active = hit.hit;
        if (none(active))
            return;

        // Global triangle index per lane.
        const Int32P gidx =
            enoki::gather<Int32P>(geomToBase.data(),
                                  enoki::reinterpret_array<Int32P>(hit.geomID),
                                  active) +
            enoki::reinterpret_array<Int32P>(hit.primID);

#if defined(__AVX512F__) || defined(__AVX2__)
        // Lane record indices.
        alignas(64) std::uint32_t recA[SimdLanes];
        enoki::store_unaligned(
            recA, enoki::select(active, enoki::reinterpret_array<UInt32P>(gidx),
                                UInt32P(0u)));
#if defined(__AVX512F__)
        const SimdVec zm = _mm512_castsi512_ps(
            _mm512_maskz_set1_epi32(active.k, ~0u));
#else
        const SimdVec zm = _mm256_castsi256_ps(active.m);
#endif
        alignas(64) SimdVec f[TRI_WORDS];
        LoadRecords<TRI_WORDS>(f, triRecords.data(), recA, zm);
        ShadeFromFields(
            hit, [&](std::uint32_t w) { return FloatP(f[w]); },
            [&](std::uint32_t w)
            { return UInt32P(RecBits(f[w])); });
#else
        ShadeHit_gather(hit);
#endif
    }

    // The expensive scatter/gather version for reference.
    void EmbreeScene::ShadeHit_gather(HitP &hit) const
    {
        const MaskP active = hit.hit;
        if (none(active))
            return;

        const Int32P gidx =
            enoki::gather<Int32P>(geomToBase.data(),
                                  enoki::reinterpret_array<Int32P>(hit.geomID),
                                  active) +
            enoki::reinterpret_array<Int32P>(hit.primID);
        const Int32P base = gidx * Int32P(TRI_WORDS);

        ShadeFromFields(
            hit,
            [&](std::uint32_t w)
            {
                return enoki::gather<FloatP>(
                    reinterpret_cast<const float *>(triRecords.data()),
                    base + Int32P(w), active);
            },
            [&](std::uint32_t w)
            { return enoki::gather<UInt32P>(triRecords.data(), base + Int32P(w),
                                            active); });
    }

    namespace
    {
        // Resolve the medium on the origin side of the first interface
        // crossed by a ray from p along unit dir.
        bool CastMedium(const RTCScene scene, const Point3f &p,
                        const Vector3f &dir,
                        const std::vector<std::uint32_t> &base,
                        const std::uint32_t *recs,
                        std::uint32_t *out)
        {
            float tcur = 0.f;
            for (int hop = 0; hop < 16; ++hop)
            {
                RTCRayHit rh{};
                rh.ray.org_x = p.x();
                rh.ray.org_y = p.y();
                rh.ray.org_z = p.z();
                rh.ray.dir_x = dir.x();
                rh.ray.dir_y = dir.y();
                rh.ray.dir_z = dir.z();
                rh.ray.tnear = tcur + EPS_RAY;
                rh.ray.tfar = INF_F;
                rh.ray.time = 0.f;
                rh.ray.mask = 0xFFFFFFFFu;
                rh.ray.flags = 0;
                rh.hit.geomID = RTC_INVALID_GEOMETRY_ID;

                RTCIntersectArguments args;
                rtcInitIntersectArguments(&args);
                args.flags = RTC_RAY_QUERY_FLAG_INCOHERENT;
                rtcIntersect1(scene, &rh, &args);

                if (rh.hit.geomID == RTC_INVALID_GEOMETRY_ID)
                    return false; // escaped: no boundary this direction

                const std::size_t g =
                    base[rh.hit.geomID] + rh.hit.primID;
                const std::uint32_t *rec = recs + g * TRI_WORDS;
                const std::uint32_t f = rec[kTri_medFront],
                    b = rec[kTri_medBack];
                if (f == b)
                {
                    // Non-boundary triangle (emitter, wall).
                    tcur = rh.ray.tfar;
                    continue;
                }

                const float dnd = dir.x() * rh.hit.Ng_x + dir.y() * rh.hit.Ng_y +
                                  dir.z() * rh.hit.Ng_z;
                // dot(d, ng) > 0: ray leaves through the back side, so p sits
                // on the back half space.
                *out = (dnd > 0.f) ? b : f;
                return true;
            }
            std::cerr << "[lux2 MediumIDAt] boundary skip limit reached at ("
                      << p.x() << ", " << p.y() << ", " << p.z()
                      << "); treating as outside\n";
            return false;
        }
    } // namespace

    std::uint32_t EmbreeScene::MediumIDAt(const Point3f &p) const
    {
        if (!m_scene)
            return LUX2_MEDIUM_CLEAR;

        const Vector3f dirs[6] = {Vector3f(1.f, 0.f, 0.f), Vector3f(-1.f, 0.f, 0.f),
                                  Vector3f(0.f, 1.f, 0.f), Vector3f(0.f, -1.f, 0.f),
                                  Vector3f(0.f, 0.f, 1.f), Vector3f(0.f, 0.f, -1.f)};

        std::uint32_t vote[2] = {LUX2_MEDIUM_CLEAR, LUX2_MEDIUM_CLEAR};
        int votes[2] = {0, 0};
        for (int i = 0; i < 6; ++i)
        {
            std::uint32_t med = LUX2_MEDIUM_CLEAR;
            if (!CastMedium(m_scene, p, dirs[i], geomToBase,
                            triRecords.data(), &med))
                med = LUX2_MEDIUM_CLEAR;

            if (votes[0] == 0 || med == vote[0])
            {
                vote[0] = med;
                votes[0]++;
            }
            else if (votes[1] == 0 || med == vote[1])
            {
                vote[1] = med;
                votes[1]++;
            }
            // A third distinct id is impossible for a point.
        }

        const std::uint32_t win = (votes[0] >= votes[1]) ? vote[0] : vote[1];
        if (vote[0] != vote[1] && votes[0] == votes[1])
        {
            std::cerr << "[lux2 MediumIDAt] split medium vote at (" << p.x()
                      << ", " << p.y() << ", " << p.z()
                      << ") — leaky or overlapping volume boundary\n";
        }
        return win;
    }

} // namespace lux2