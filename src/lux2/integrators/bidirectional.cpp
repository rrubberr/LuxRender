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

#include "integrators/bidirectional.h"
#include "integrators/debug/integrator_debug.h"
#include "integrators/core/vertex.h"

#include "core/bsdf_call.h"
#include "core/bsdfptr_table.h"
#include "core/camera.h"
#include "core/color/color.h"
#include "core/dynload.h"
#include "core/accel/embree2.h"
#include "core/light.h"
#include "core/light_call.h"
#include "core/math/math.h"
#include "core/medium.h"
#include "core/mediumptr_table.h"
#include "core/register.h"
#include "core/rng.h"
#include "core/sampling.h"
#include "core/scene.h"

#include <enoki/array.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace lux2
{
    namespace
    {
        // a/b, 0 where b <= 0.
        inline FloatP divp(const FloatP &a, const FloatP &b)
        {
            return select(b > FloatP(0.f), a / b, FloatP(0.f));
        }

        inline Point3fP OffsetRay(const Point3fP &p, const Normal3fP &ng,
                                  const Vector3fP &d, FloatP k)
        {
            const Normal3fP s = select(dot(d, ng) > FloatP(0.f), ng, -ng);
            return p + Vector3fP(s.x(), s.y(), s.z()) * k;
        }

        // Solid angle density at p -> area density at pNext (normal ngNext).
        inline FloatP ConvertDensity(const Point3fP &p, FloatP pdf,
                                     const Point3fP &pNext,
                                     const Normal3fP &ngNext)
        {
            const Vector3fP d = pNext - p;
            const FloatP d2 = dot(d, d);
            const FloatP invD2 = divp(FloatP(1.f), d2);
            const Vector3fP w = d * enoki::sqrt(invD2);
            return pdf * abs(dot(ngNext, w)) * invD2;
        }

        inline BSDFEvalP ZeroEval()
        {
            BSDFEvalP e;
            e.f = SWCSpectrumP(0.f);
            e.pdf = FloatP(0.f);
            e.pdfRev = FloatP(0.f);
            return e;
        }

        // Power heuristic term p^k.
        inline FloatP pw(const FloatP &p, float k)
        {
            if (k == 2.f)
                return p * p;
            if (k == 1.f)
                return p;
            if (k == 0.f)
                return FloatP(1.f);
            return enoki::pow(p, FloatP(k));
        }

        // ------------------------------------------------------------------
        // Pixel jobs
        // ------------------------------------------------------------------
        struct Job
        {
            int px, py;
            uint32_t s;
        };

        struct JobCursor
        {
            int x0, y0, x1, y1;
            uint32_t spp;
            int px, py;
            uint32_t s = 0;

            JobCursor(const Tile &t, uint32_t n)
                : x0(t.x0), y0(t.y0), x1(t.x1), y1(t.y1), spp(n),
                  px(t.x0), py(t.y0) {}

            bool Next(Job &j)
            {
                if (s >= spp || x1 <= x0 || y1 <= y0)
                    return false;
                j = Job{px, py, s};
                if (++px == x1)
                {
                    px = x0;
                    if (++py == y1)
                    {
                        py = y0;
                        ++s;
                    }
                }
                return true;
            }
        };

        // ------------------------------------------------------------------
        // Subpath rows
        // ------------------------------------------------------------------
        struct Subpath
        {
            std::vector<Point3fP> p;
            std::vector<DifferentialGeometryP> dg;
            std::vector<BSDFPtr> bsdf;
            std::vector<Vector3fP> wb;      // back along incoming segment
            std::vector<SWCSpectrumP> flux; // RR adjusted flux
            std::vector<FloatP> pdf, pdfR;
            std::vector<FloatP> cosi, coso; // |dot(., ng)|
            std::vector<FloatP> d2;         // squared distance to next vertex
            std::vector<FloatP> dAWeight, dARWeight;
            std::vector<FloatP> rr, rrR;
            std::vector<MaskP> spec;    // sampled lobe was specular
            UInt32P selIdx;             // light chosen for the origin
            FloatP directPdf1;          // light->Pdf(light[1].p, light[0]) * sel,
                                        // area measure at light[0]
            std::vector<MaskP> isMed;   // marks a volume scatter vertex
            std::vector<MediumPtr> med; // medium the vertex sits in
            std::vector<FloatP> k;      // Schlick k resolved at the scatter
            // Interface vertices store both sides.
            std::vector<UInt32P> medFrontID; // side ng points to
            std::vector<UInt32P> medBackID;
            // The medium containing the vertex.
            std::vector<UInt32P> medID;

            void Init(int n)
            {
                p.assign(n, Point3fP(0.f));
                dg.resize(n);
                bsdf.assign(n, BSDFPtr(nullptr));
                wb.assign(n, Vector3fP(0.f));
                flux.assign(n, SWCSpectrumP(0.f));
                pdf.assign(n, FloatP(0.f));
                pdfR.assign(n, FloatP(0.f));
                cosi.assign(n, FloatP(0.f));
                coso.assign(n, FloatP(0.f));
                d2.assign(n, FloatP(0.f));
                dAWeight.assign(n, FloatP(0.f));
                dARWeight.assign(n, FloatP(0.f));
                rr.assign(n, FloatP(1.f)); // legacy constructor default
                rrR.assign(n, FloatP(1.f));
                spec.assign(n, MaskP(false));
                isMed.assign(n, MaskP(false));
                med.assign(n, MediumPtr(nullptr));
                k.assign(n, FloatP(0.f));
                medFrontID.assign(n, UInt32P(LUX2_MEDIUM_CLEAR));
                medBackID.assign(n, UInt32P(LUX2_MEDIUM_CLEAR));
                medID.assign(n, UInt32P(LUX2_MEDIUM_CLEAR));
                selIdx = UInt32P(0u);
                directPdf1 = FloatP(0.f);
            }
        };

        // Medium a ray leaves vertex idx into, given the leaving direction.
        inline UInt32P StartMedium(const Subpath &sp, int idx,
                                   const Vector3fP &dir)
        {
            const MaskP front = dot(dir, sp.dg[idx].ng) > FloatP(0.f);
            const UInt32P surfMed =
                select(front, sp.medFrontID[idx], sp.medBackID[idx]);
            return select(sp.isMed[idx], sp.medID[idx], surfMed);
        }

        struct WalkCtx
        {
            const Scene *scene;
            const EmbreeScene *embree;
            const BsdfPtrTable *table;
            const Camera *camera = nullptr;
            int nLights;
            // Light sampling tables for the active group.
            const LightsSamplingStrategy *lsD = nullptr; // NEE and direct density
            const LightsSamplingStrategy *lsP = nullptr; // light subpath origin
            std::vector<const Light *> lightPtrs;
            std::vector<const Light *> areaPtrs;
            std::vector<std::int32_t> areaGroup;
            std::vector<float> areaById;  // by hit.lightID
            std::vector<float> areaByIdx; // by lightPtrs index
            int activeGroup = 0;
            bool multiGroup = false;
            int eyeDepth, lightDepth;
            FloatP eyeRR, lightRR; // eyeThreshold, lightThreshold
            bool useT1 = false;
            // Verify that uniform area conversions of a solid angle
            // pdf recover the selection pdf.
            bool checkDensity = false;
            // Id 0 is the ambient composite.
            const MediumPtrTable *mediumTable = nullptr;
            bool hasMedia = false;
            UInt32P cameraMediumID = UInt32P(LUX2_MEDIUM_CLEAR);
            // Diagnostic accumulator.
            BidirDiagLocal *diag = nullptr;
            DiagConfig cfg;
        };

        struct LaneState
        {
            SpectrumWavelengthsP sw;
            FloatP x, y, camWeight, alpha;
            UInt32P seed, sampleIdx;
            SWCSpectrumP L;
            MaskP active;
        };

        // Strategy filter.
#ifdef LUX2_INTEGRATOR_DEBUG
        inline bool want(const DiagConfig &c, Strat kind, int nE, int nL)
        {
            if (c.onlyT >= 0 && nE != c.onlyT)
                return false;
            if (c.onlyS >= 0)
            {
                const int sEff = (kind == Strat::HitLight)
                                     ? 0
                                 : (kind == Strat::NEE) ? 1
                                                        : nL;
                if (sEff != c.onlyS)
                    return false;
            }
            return true;
        }
#else
        inline bool want(const DiagConfig &, Strat, int, int)
        {
            return true;
        }
#endif

        void Refill(const Sampler &sampler, JobCursor &jobs,
                    uint64_t sampleBasis, LaneState &st, int k = 2)
        {
            const int nDead = enoki::count(!st.active);
            if (nDead < k)
                return;

            alignas(64) float aliveF[PACKET_WIDTH];
            enoki::store_unaligned(aliveF,
                                   select(st.active, FloatP(1.f), FloatP(0.f)));
            alignas(64) float fillF[PACKET_WIDTH] = {};
            alignas(64) float pxs[PACKET_WIDTH] = {}, pys[PACKET_WIDTH] = {};
            alignas(64) uint32_t seeds[PACKET_WIDTH] = {},
                                 sidxs[PACKET_WIDTH] = {};

            const uint64_t baseSeed = sampler.BaseSeed();
            bool anyFill = false;
            for (size_t i = 0; i < PACKET_WIDTH; ++i)
            {
                if (aliveF[i] > 0.f)
                    continue;
                Job j;
                if (!jobs.Next(j))
                    break;
                fillF[i] = 1.f;
                pxs[i] = float(j.px);
                pys[i] = float(j.py);
                sidxs[i] = j.s;
                const uint64_t h = PixelSeed(sampleBasis, baseSeed, j.px, j.py);
                seeds[i] = uint32_t(h >> 32);
                anyFill = true;
            }
            if (!anyFill)
                return;

            const MaskP fillMask =
                enoki::load_unaligned<FloatP>(fillF) > FloatP(0.5f);
            const UInt32P vseed = enoki::load_unaligned<UInt32P>(seeds);
            const UInt32P vidx = enoki::load_unaligned<UInt32P>(sidxs);
            const FloatP pxf = enoki::load_unaligned<FloatP>(pxs);
            const FloatP pyf = enoki::load_unaligned<FloatP>(pys);

            const Point2fP jit = sampler.Get2D(vseed, vidx, UInt32P(0u));
            SpectrumWavelengthsP newSw;
            newSw.Sample(sampler.Get1D(vseed, vidx, UInt32P(1u)));

            st.x = select(fillMask, pxf + jit.x(), st.x);
            st.y = select(fillMask, pyf + jit.y(), st.y);
            st.camWeight = select(fillMask, FloatP(1.f), st.camWeight);
            st.alpha = select(fillMask, FloatP(0.f), st.alpha);
            st.seed = select(fillMask, vseed, st.seed);
            st.sampleIdx = select(fillMask, vidx, st.sampleIdx);
            st.L = select(fillMask, SWCSpectrumP(0.f), st.L);
            st.sw.w = select(fillMask, newSw.w, st.sw.w);
            st.sw.binRGB = select(fillMask, newSw.binRGB, st.sw.binRGB);
            st.sw.offsetRGB = select(fillMask, newSw.offsetRGB, st.sw.offsetRGB);
            st.sw.binXYZ = select(fillMask, newSw.binXYZ, st.sw.binXYZ);
            st.sw.offsetXYZ = select(fillMask, newSw.offsetXYZ, st.sw.offsetXYZ);
            st.active = st.active || fillMask;
        }

        struct RetireBuffer
        {
            static constexpr size_t CAP = 3 * PACKET_WIDTH;
            float bx[CAP], by[CAP], bL[CAP], bwl[CAP], ba[CAP], bwt[CAP];
            size_t n = 0;

            void Push(const LaneState &st, const MaskP &finished)
            {
                float *px = bx + n, *py = by + n, *pL = bL + n;
                float *pwl = bwl + n, *pa = ba + n, *pwt = bwt + n;
                const size_t c = enoki::compress(px, st.x, finished);
                enoki::compress(py, st.y, finished);
                enoki::compress(pL, st.L, finished);
                enoki::compress(pwl, st.sw.w, finished);
                enoki::compress(pa, st.alpha, finished);
                enoki::compress(pwt, st.camWeight, finished);
                n += c;
            }

            void Flush(Film &dest, bool all)
            {
                while (n >= PACKET_WIDTH || (all && n > 0))
                {
                    const size_t take =
                        n < PACKET_WIDTH ? n : size_t(PACKET_WIDTH);
                    const MaskP real =
                        enoki::arange<FloatP>() < FloatP(float(take));
                    auto loadMasked = [&](const float *b)
                    { return select(real, enoki::load_unaligned<FloatP>(b),
                                    FloatP(0.f)); };
                    SpectrumWavelengthsP sw;
                    sw.FromWavelength(loadMasked(bwl));
                    dest.Splat(loadMasked(bx), loadMasked(by),
                               SWCSpectrumP(loadMasked(bL)), sw,
                               loadMasked(ba), loadMasked(bwt), 0);
                    const size_t rem = n - take;
                    std::memmove(bx, bx + take, rem * sizeof(float));
                    std::memmove(by, by + take, rem * sizeof(float));
                    std::memmove(bL, bL + take, rem * sizeof(float));
                    std::memmove(bwl, bwl + take, rem * sizeof(float));
                    std::memmove(ba, ba + take, rem * sizeof(float));
                    std::memmove(bwt, bwt + take, rem * sizeof(float));
                    n = rem;
                }
            }
        };

        // RNG dimension layout.
        inline UInt32P EyeDim(int v) { return UInt32P(uint32_t(2 + (v - 1) * 4)); }
        inline int LightOriginBaseI(int eyeDepth)
        {
            return 2 + std::max(0, eyeDepth - 1) * 4;
        }
        inline UInt32P LightBounce(int loBase, int s)
        {
            return UInt32P(uint32_t(loBase + 6 + (s - 1) * 4));
        }
        inline int NeeBaseI(int loBase, int lightDepth)
        {
            return loBase + 6 + std::max(0, lightDepth - 1) * 4;
        }
        inline UInt32P NeeDim(int neeBase, int ei)
        {
            return UInt32P(uint32_t(neeBase + (ei - 1) * 4));
        }
        // Camera NEE block.
        inline int CamNeeBaseI(int neeBase, int eyeDepth)
        {
            return neeBase + 4 * std::max(0, eyeDepth - 1);
        }

        // RNG dimension namespace for "all" light subpaths.
        constexpr uint32_t LIGHT_ALL_BASE = 1u << 22;

        // Per-slot origin base for an "all" light subpath.
        inline int AllLightBase(const WalkCtx &c, uint32_t slot)
        {
            return int(LIGHT_ALL_BASE +
                       slot * uint32_t(6 + 4 * std::max(0, c.lightDepth - 1)));
        }

        // RNG dimension namespace for "all" NEE slots.
        constexpr uint32_t NEE_ALL_BASE = 1u << 20;

        inline DifferentialGeometryP MakeDG(const HitP &hit, const Vector3fP &rayD,
                                            const MaskP &m)
        {
            DifferentialGeometryP dg;
            dg.p = hit.p;
            dg.n = select(m, hit.sh_n, Normal3fP(0.f));
            dg.ng = select(m, hit.ngeo, Normal3fP(0.f));
            dg.entering = dot(rayD, hit.ngeo) < FloatP(0.f);
            dg.uv_u = hit.uv.x();
            dg.uv_v = hit.uv.y();
            dg.dp_du = hit.dp_du;
            dg.dp_dv = hit.dp_dv;
            dg.dp_ds = hit.dp_ds;
            dg.dp_dt = hit.dp_dt;
            return dg;
        }

        // Medium on the side dir leaves through, given a boundary hit.
        inline UInt32P SwapMedium(const HitP &hit, const Vector3fP &dir)
        {
            const MaskP leavesFront = dot(dir, hit.ngeo) > FloatP(0.f);
            return select(leavesFront, hit.mediumFrontID, hit.mediumBackID);
        }

        // Medium flight outcome of IntersectCollapsed.
        struct FlightResult
        {
            MaskP mediumEvent;     // scattered before the segment's surface
            MaskP absorbed;        // lane retires
            MediumPtr med;         // medium of the scatter
            Point3fP p;            // scatter position
            FloatP k;              // Schlick k at the scatter
            SWCSpectrumP weight;   // multiply into beta
            SWCSpectrumP emission; // add to L at the preflight beta
        };

        inline void ResetFlight(FlightResult *fr)
        {
            fr->mediumEvent = MaskP(false);
            fr->absorbed = MaskP(false);
            fr->med = MediumPtr(nullptr);
            fr->p = Point3fP(0.f);
            fr->k = FloatP(0.f);
            fr->weight = SWCSpectrumP(1.f);
            fr->emission = SWCSpectrumP(0.f);
        }

        // Evaluate s==0 so an emissive Null surface is not lost.
        template <class OnHit>
        void IntersectCollapsed(const WalkCtx &ctx, RayP &ray, const MaskP &alive,
                                const LaneState &st, const Int32P &bounce,
                                const Int32P &depthBase, SWCSpectrumP *beta,
                                SWCSpectrumP *Lout, HitP &hit,
                                FlightResult &fr, OnHit &&onHit)
        {
            MaskP tracing = alive;
            ResetFlight(&fr);
            for (int c = 0; c <= MAX_NULL_CROSSINGS && any(tracing); ++c)
            {
                ray.alive = tracing;
                HitP h;
                ctx.embree->Intersect(ray, h, Coherent::No);
                ctx.embree->ShadeHit(ray, h);
                const MaskP didHit = tracing && h.hit;

                // Flight over [ray.mint, nearest surface] in ray.medium.
                MaskP stop(false);
                if (ctx.hasMedia)
                {
                    const MaskP inMed = tracing && ctx.mediumTable->GatherNonVacuum(
                                                       ray.medium, tracing);
                    if (any(inMed))
                    {
                        const FloatP tEnd =
                            select(didHit, h.t, FloatP(INF_F));
                        MediumSampleP ms;
                        ClearMediumSample(&ms, tEnd);
                        const MediumPtr med =
                            ctx.mediumTable->Gather(ray.medium, inMed);
                        RNGP rng = KeyedRNG(st.seed, st.sampleIdx, bounce,
                                            Int32P(c) + depthBase, UInt32P(0u),
                                            FLIGHT_SALT);
                        const FloatP uFlight = rng.NextFloat(inMed);
                        med->SampleInteraction(st.sw, ray, ray.mint, tEnd,
                                               uFlight, rng, &ms, inMed);
                        if (Lout)
                            *Lout = *Lout +
                                    select(inMed, (*beta) * ms.emission,
                                           SWCSpectrumP(0.f));
                        *beta = select(inMed, (*beta) * ms.weight, *beta);
                        fr.absorbed = fr.absorbed || (inMed && ms.absorbed);
                        const MaskP scat = inMed && ms.scattered;
                        if (any(scat))
                        {
                            fr.mediumEvent = fr.mediumEvent || scat;
                            // Keep medium pointer a lane already carried.
                            fr.med = select(scat, med, fr.med);
                            fr.p = select(scat, ms.p, fr.p);
                            fr.k = select(scat, ms.k, fr.k);
                        }
                        stop = inMed && (ms.scattered || ms.absorbed);
                    }
                }
                // A lane stopped inside the medium.
                tracing = tracing && !stop;
                const MaskP seen = didHit && !stop;
                onHit(h, ray.d, seen);

                MaskP isNull(false);
                if (c < MAX_NULL_CROSSINGS && any(seen))
                {
                    const BSDFPtr b = ctx.table->Gather(h.matID, seen);
                    isNull = seen && has_flag(b->flags(), BSDFType::Null);
                }
                if (c == 0)
                    hit = h; // initialize every field/lane
                const MaskP done = tracing && !isNull;
                hit.hit = select(done, h.hit, hit.hit);
                hit.p = select(done, h.p, hit.p);
                hit.ngeo = select(done, h.ngeo, hit.ngeo);
                hit.sh_n = select(done, h.sh_n, hit.sh_n);
                hit.uv = select(done, h.uv, hit.uv);
                hit.matID = select(done, h.matID, hit.matID);
                hit.lightID = select(done, h.lightID, hit.lightID);
                hit.dp_du = select(done, h.dp_du, hit.dp_du);
                hit.dp_dv = select(done, h.dp_dv, hit.dp_dv);
                hit.dp_ds = select(done, h.dp_ds, hit.dp_ds);
                hit.dp_dt = select(done, h.dp_dt, hit.dp_dt);
                hit.mediumFrontID =
                    select(done, h.mediumFrontID, hit.mediumFrontID);
                hit.mediumBackID =
                    select(done, h.mediumBackID, hit.mediumBackID);
                ray.o = select(isNull,
                               OffsetRay(h.p, h.ngeo, ray.d, FloatP(EPS_RAY)),
                               ray.o);
                if (ctx.hasMedia && any(isNull))
                    ray.medium =
                        select(isNull, SwapMedium(h, ray.d), ray.medium);
                tracing = isNull;
            }
            // Stopped lanes report no surface hit.
            hit.hit = hit.hit && !(fr.mediumEvent || fr.absorbed);
        }

        // Medium aware transmittance origin->dir over [EPS, tMax].
        SWCSpectrumP
        ConnectTr(const WalkCtx &ctx, const Point3fP &origin,
                  const Vector3fP &dir, const UInt32P &startMedID,
                  const FloatP &tMax, const SpectrumWavelengthsP &sw,
                  const LaneState &st, const Int32P &bounce,
                  const UInt32P &connKey, UInt64 salt, MaskP active)
        {
            if (!ctx.hasMedia)
            {
                RayP shadow(origin, dir, FloatP(EPS_RAY), tMax, FloatP(0.f));
                shadow.wavelengths = sw.w;
                shadow.mask = UInt32P(0xFFFFFFFFu);
                const MaskP blocked =
                    ctx.embree->Occluded(shadow, active, Coherent::No);
                return select(active && !blocked, SWCSpectrumP(1.f),
                              SWCSpectrumP(0.f));
            }
            SWCSpectrumP T(1.f);
            UInt32P curMedium = startMedID;
            FloatP tCur = FloatP(EPS_RAY);
            MaskP tracing = active;
            RNGP rng = KeyedRNG(st.seed, st.sampleIdx, bounce, Int32P(0),
                                connKey, salt);
            for (int i = 0; i < MAX_SHADOW_CROSSINGS && any(tracing); ++i)
            {
                RayP shadow(origin, dir, tCur, tMax, FloatP(0.f));
                shadow.wavelengths = sw.w;
                shadow.mask = UInt32P(0xFFFFFFFFu);
                HitP hit;
                ctx.embree->Intersect(shadow, hit, Coherent::No);
                ctx.embree->ShadeHit(shadow, hit);
                const MaskP didHit = tracing && hit.hit;
                const MaskP missed = tracing && !hit.hit;

                // Transmittance over the interval.
                const MediumPtr med =
                    ctx.mediumTable->Gather(curMedium, tracing);
                const FloatP segEnd = select(didHit, hit.t, tMax);
                T = select(tracing,
                           T * med->Transmittance(sw, shadow, tCur, segEnd,
                                                  rng, tracing),
                           T);

                // Missed everything, reached the light, lane is done.
                tracing = tracing && !missed;
                if (none(tracing))
                    break;

                const BSDFPtr bsdf = ctx.table->Gather(hit.matID, tracing);
                const MaskP opaque =
                    tracing && !has_flag(bsdf->flags(), BSDFType::Null);
                T = select(opaque, SWCSpectrumP(0.f), T);
                tracing = tracing && !opaque;
                if (any(tracing))
                {
                    curMedium = SwapMedium(hit, dir);
                    tCur = select(tracing, hit.t + FloatP(EPS_RAY), tCur);
                }
            }
            // Lanes still tracing after the crossing cap is occluded.
            T = select(tracing, SWCSpectrumP(0.f), T);
            return T;
        }

        // ------------------------------------------------------------------
        // Transient
        // ------------------------------------------------------------------
        struct Transient
        {
            FloatP eAF_last;            // eye.dARWeight[nE-1]
            FloatP eAR_last, eAR_prev;  // eye.dAWeight[nE-1], [nE-2]
            FloatP eRR_last, eRRR_last; // eye.rr/rrR[nE-1]
            FloatP lAR_last, lAR_prev;  // light.dARWeight[nL-1], [nL-2]
            FloatP lRR_last, lRRR_last; // light.rr/rrR[nL-1]
            FloatP pdfLightDirect;      // area measure including selection
            bool isLightDirect = false;
            // Connection vertices are non-specular.

            Transient()
                : eAF_last(0.f), eAR_last(0.f), eAR_prev(0.f), eRR_last(1.f),
                  eRRR_last(1.f), lAR_last(0.f), lAR_prev(0.f), lRR_last(1.f),
                  lRRR_last(1.f), pdfLightDirect(0.f) {}
        };

        // ------------------------------------------------------------------
        // WeightPath
        // ------------------------------------------------------------------
        FloatP WeightPath(const WalkCtx &ctx, const Subpath &eye, int nE,
                          const Subpath &light, int nL, const Transient &tr,
                          Breakdown *bd = nullptr)
        {
            if (ctx.cfg.noMis)
                return FloatP(1.f); // w = 1 isolates each strategy's raw Lc

            const float k = ctx.cfg.misPower;
            auto eAF = [&](int idx) -> FloatP
            { return idx == nE - 1 ? tr.eAF_last : eye.dARWeight[idx]; };
            auto eAR = [&](int idx) -> FloatP
            { return idx == nE - 1 ? tr.eAR_last
                                   : (idx == nE - 2 ? tr.eAR_prev : eye.dAWeight[idx]); };
            auto eRR = [&](int idx) -> FloatP
            { return idx == nE - 1 ? tr.eRR_last : eye.rr[idx]; };
            auto eRRR = [&](int idx) -> FloatP
            { return idx == nE - 1 ? tr.eRRR_last : eye.rrR[idx]; };
            auto eSpec = [&](int idx) -> MaskP
            { return idx == nE - 1 ? MaskP(false) : eye.spec[idx]; };

            auto lAF = [&](int idx) -> FloatP
            { return light.dAWeight[idx]; };
            auto lAR = [&](int idx) -> FloatP
            { return idx == nL - 1 ? tr.lAR_last
                                   : (idx == nL - 2 ? tr.lAR_prev : light.dARWeight[idx]); };
            auto lRR = [&](int idx) -> FloatP
            { return idx == nL - 1 ? tr.lRR_last : light.rr[idx]; };
            auto lRRR = [&](int idx) -> FloatP
            { return idx == nL - 1 ? tr.lRRR_last : light.rrR[idx]; };
            auto lSpec = [&](int idx) -> MaskP
            { return idx == nL - 1 ? MaskP(false) : light.spec[idx]; };

            const FloatP aF0 = nL >= 1 ? lAF(0) : FloatP(0.f);
            const FloatP pBase = (nL == 1 && tr.isLightDirect)
                                     ? divp(aF0, tr.pdfLightDirect)
                                     : FloatP(1.f);
            FloatP weight(1.f);
            FloatP p = pBase;
            if (bd)
                bd->base = pBase;

            if (nL == 1)
            {
                if (tr.isLightDirect)
                {
                    // light[0] flags are forced non-specular by EvalPath.
                    if (ctx.lightDepth > 0)
                    {
                        const MaskP m = aF0 > FloatP(0.f);
                        const FloatP add = pw(pBase, k);
                        weight = select(m, weight + add, weight);
                        if (bd)
                            bd->directOrigin = select(m, add, FloatP(0.f));
                    }
                }
                else
                {
                    const FloatP pDirect = divp(tr.pdfLightDirect, aF0);
                    const MaskP m = aF0 > FloatP(0.f);
                    const FloatP add = pw(pDirect, k);
                    weight = select(m, weight + add, weight);
                    if (bd)
                        bd->directOrigin = select(m, add, FloatP(0.f));
                }
            }

            // Eye path hit a light.
            if (nL == 0 && (nE - 2 >= 1 || ctx.useT1))
            {
                FloatP pDirect = divp(tr.pdfLightDirect, eAF(nE - 1));
                if (nE > RR_START + 1)
                    pDirect = divp(pDirect, eye.rrR[nE - 2]);
                const MaskP m = !eSpec(nE - 2);
                const FloatP add = pw(pDirect, k);
                weight = select(m, weight + add, weight);
                if (bd)
                    bd->hitLightAlt = select(m, add, FloatP(0.f));
            }

            // Extend the light path toward the eye path.
            const int minEye = ctx.useT1 ? 1 : 2;
            const int nLightExt =
                std::min(nE - minEye,
                         ctx.lightDepth - std::min(ctx.lightDepth, nL));
            MaskP ok(true);
            for (int i = 1; i <= nLightExt; ++i)
            {
                const int idx = nE - i;
                ok = ok && (eAR(idx) > FloatP(0.f)) && (eAF(idx) > FloatP(0.f));
                p = p * divp(eAR(idx), eAF(idx));
                if (idx > RR_START)
                    p = divp(p, eye.rrR[idx - 1]);
                if (nL + i > RR_START + 1)
                    p = p * (i == 1 ? lRR(nL - 1) : eRR(idx + 1));
                const MaskP specOk = !eSpec(idx) && (i == nE ? MaskP(true) : !eSpec(idx - 1));
                const MaskP m = ok && specOk;
                const FloatP add = pw(p, k);
                weight = select(m, weight + add, weight);
                if (bd)
                    bd->lightExt += select(m, add, FloatP(0.f));
            }
            if (bd)
                bd->okLight = ok;

            // Extend the eye path toward the light path.
            p = pBase;
            const int nEyeExt =
                std::min(nL, ctx.eyeDepth - std::min(ctx.eyeDepth, nE));
            ok = MaskP(true);
            for (int i = 1; i <= nEyeExt; ++i)
            {
                const int idx = nL - i;
                ok = ok && (lAR(idx) > FloatP(0.f)) && (lAF(idx) > FloatP(0.f));
                p = p * divp(lAR(idx), lAF(idx));
                if (idx > RR_START)
                    p = divp(p, lRR(idx - 1));
                if (nE + i > RR_START + 1)
                    p = p * (i == 1 ? eRRR(nE - 1) : lRRR(idx + 1));
                const MaskP specOk = !lSpec(idx) && (i == nL ? MaskP(true) : !lSpec(idx - 1));
                const MaskP m = ok && specOk;
                const FloatP add = pw(p, k);
                weight = select(m, weight + add, weight);
                if (bd)
                    bd->eyeExt += select(m, add, FloatP(0.f));
                // Direct path's second vertex must be non-specular.
                if (i == nL - 1)
                {
                    const MaskP dm = ok && !lSpec(1) && (aF0 > FloatP(0.f));
                    const FloatP pDirect = p * divp(tr.pdfLightDirect, aF0);
                    const FloatP add2 = pw(pDirect, k);
                    weight = select(dm, weight + add2, weight);
                    if (bd)
                        bd->directPath = select(dm, add2, FloatP(0.f));
                }
            }
            if (bd)
                bd->okEye = ok;
            return weight;
        }

        // ------------------------------------------------------------------
        // Eye path landed on an emitter
        // ------------------------------------------------------------------
        void AccumulateS0(const WalkCtx &ctx, LaneState &st, const Subpath &eye,
                          int t, const SWCSpectrumP &beta, const HitP &h,
                          const Vector3fP &rayD, const MaskP &emitIn)
        {
            if (!want(ctx.cfg, Strat::HitLight, t + 1, 0))
                return;
            MaskP emit = emitIn;
            if (!any(emit))
                return;
            if (ctx.multiGroup)
            {
                const Int32P g = enoki::gather<Int32P>(ctx.areaGroup.data(),
                                                       h.lightID, emit);
                emit = emit && eq(g, Int32P(ctx.activeGroup));
            }
            if (!any(emit))
                return;

            const DifferentialGeometryP dg = MakeDG(h, rayD, emit);
            const Vector3fP wo = -rayD;
            const LightPtr al =
                enoki::gather<LightPtr>(ctx.areaPtrs.data(), h.lightID, emit);
            const SWCSpectrumP Le = al->Le(dg, st.sw, wo, emit);
            const SWCSpectrumP Lc = beta * Le; // legacy: Ll(v.flux) * Le
            const MaskP cont = emit && (Lc > SWCSpectrumP(0.f));
            if (!any(cont))
                return;

            const int nE = t + 1;
            const Vector3fP dv = h.p - eye.p[t - 1];
            const FloatP d2 = dot(dv, dv); // legacy vp.d2
            const FloatP invD2 = divp(FloatP(1.f), d2);
            const Vector3fP wDir = dv * enoki::sqrt(invD2);
            const FloatP cB = abs(dot(wDir, h.ngeo)); // v.coso

            const FloatP area =
                enoki::gather<FloatP>(ctx.areaById.data(), h.lightID, cont);
            const FloatP emitPdf = abs(dot(wo, dg.n)) * INVPI; // v.pdf

            Transient tr;
            // v.dARWeight = vp.pdfR * vp.tPdfR / vp.d2 * v.coso
            tr.eAF_last = (t >= 2 || ctx.useT1)
                              ? eye.pdfR[t - 1] * cB * invD2
                              : FloatP(0.f);
            // v.dAWeight = pdfArea * lightPathStrategy->Pdf
            const FloatP pLP =
                enoki::gather<FloatP>(ctx.lsP->areaPSel.data(), h.lightID, cont);
            tr.eAR_last = pLP * divp(FloatP(1.f), area);
            // vp.dAWeight = v.pdf * v.tPdf / vp.d2 * vp.cosi
            tr.eAR_prev = emitPdf * eye.cosi[t - 1] * invD2;
            tr.eRR_last = FloatP(1.f);
            tr.eRRR_last = FloatP(1.f);
            // ePdfDirect = direct sampling area density of point from vp,
            // times light-selection pdf.
            const FloatP pD =
                enoki::gather<FloatP>(ctx.lsD->areaPSel.data(), h.lightID, cont);
            const FloatP pdfSolid =
                al->Pdf_L(eye.p[t - 1], eye.dg[t - 1].n, h.p, dg.n, cont,
                          MaskP(t > 1) && !eye.isMed[t - 1]) *
                pD;
            tr.pdfLightDirect = ConvertDensity(eye.p[t - 1], pdfSolid, h.p, h.ngeo);
            tr.isLightDirect = false;

            Breakdown bd;
            const FloatP w = divp(FloatP(1.f),
                                  WeightPath(ctx, eye, nE, eye, 0, tr,
                                             ctx.diag ? &bd : nullptr));
            st.L = select(cont, st.L + Lc * w, st.L);
            BidirRecordFirefly(ctx.diag, ctx.cfg.threshold, st.x, st.y, cont,
                               Lc, w, d2, /*s*/ 0, nE, &bd);
        }

        // ------------------------------------------------------------------
        // Eye subpath
        // ------------------------------------------------------------------
        void TraceEye(const WalkCtx &ctx, const Camera &camera,
                      const Sampler &sampler, LaneState &st, Subpath &eye,
                      Int32P *nEyeOut)
        {
            const int maxV = ctx.eyeDepth;
            const MaskP active = st.active;

            RayP primary;
            FloatP camW;
            camera.GenerateRay(st.x, st.y, FloatP(0.f), &primary, &camW, active);
            primary.wavelengths = st.sw.w;
            primary.mask = UInt32P(0xFFFFFFFFu);
            st.camWeight = select(active, camW, st.camWeight);

            eye.p[0] = primary.o;
            eye.dg[0].p = primary.o;
            eye.dg[0].n = Normal3fP(0.f);
            eye.dg[0].ng = Normal3fP(0.f);
            eye.bsdf[0] = BSDFPtr(nullptr);
            eye.wb[0] = Vector3fP(0.f);
            eye.flux[0] = SWCSpectrumP(1.f);
            eye.spec[0] = MaskP(false);
            if (ctx.useT1)
            {
                eye.pdfR[0] = camera.Pdf(primary, active);
                eye.pdf[0] = eye.pdfR[0]; // symmetric for pinhole
            }
            else
            {
                eye.pdf[0] = eye.pdfR[0] = FloatP(0.f);
            }
            eye.rr[0] = FloatP(1.f);
            eye.rrR[0] = FloatP(1.f);
            eye.cosi[0] = eye.coso[0] = FloatP(0.f);
            eye.dAWeight[0] = eye.dARWeight[0] = FloatP(0.f);

            Int32P nEye = Int32P(1);
            MaskP alive = active;
            RayP ray = primary;
            // Camera vertex start medium.
            ray.medium = ctx.cameraMediumID;
            eye.medID[0] = ctx.cameraMediumID;
            eye.medFrontID[0] = ctx.cameraMediumID;
            eye.medBackID[0] = ctx.cameraMediumID;
            SWCSpectrumP beta = eye.flux[0];

            for (int t = 1; t < maxV; ++t)
            {
                if (!any(alive))
                    break;
                // Active lanes this eye path bounce.
                OccupancyStep(enoki::count(alive));
                const UInt32P d0 = EyeDim(t);
                const Point2fP u2bsdf = sampler.Get2D(st.seed, st.sampleIdx, d0);
                const FloatP ucomp =
                    sampler.Get1D(st.seed, st.sampleIdx, d0 + UInt32P(2u));
                const FloatP urr =
                    sampler.Get1D(st.seed, st.sampleIdx, d0 + UInt32P(3u));

                HitP hit;
                FlightResult fr;
                // Emission at preflight beta into st.L, beta attenuated per segment.
                IntersectCollapsed(
                    ctx, ray, alive, st, Int32P(t), Int32P(0), &beta, &st.L,
                    hit, fr,
                    [&](const HitP &h, const Vector3fP &rd, const MaskP &didHitC)
                    {
                        const MaskP emitHere = didHitC && (h.lightID >= Int32P(0));
                        AccumulateS0(ctx, st, eye, t, beta, h, rd, emitHere);
                    });
                alive &= !fr.absorbed;

                // A scatter is a vertex too!
                const MaskP mediumEvent = fr.mediumEvent && alive;
                const MaskP didHit = alive && hit.hit;
                st.alpha = select(didHit && (t == 1), FloatP(1.f), st.alpha);
                alive &= (hit.hit || mediumEvent);
                if (!any(alive))
                    break;

                const MaskP hitMask = didHit && !mediumEvent;
                const DifferentialGeometryP dg = MakeDG(hit, ray.d, hitMask);
                const BSDFPtr bsdf = ctx.table->Gather(hit.matID, hitMask);
                const Vector3fP wo = -ray.d;
                const MaskP validVtx = alive; // hit or scatter
                nEye = select(validVtx, nEye + Int32P(1), nEye);

                // vp.d2, v.coso, v.dARWeight.
                const Point3fP pNew = select(mediumEvent, fr.p, hit.p);
                const Vector3fP dv = pNew - eye.p[t - 1];
                const FloatP d2 = dot(dv, dv);
                const FloatP cB = select(mediumEvent, FloatP(1.f),
                                         abs(dot(wo, dg.ng)));
                eye.d2[t - 1] = select(validVtx, d2, eye.d2[t - 1]);
                eye.p[t] = pNew;
                eye.dg[t] = dg;
                eye.bsdf[t] = bsdf; // null on scatter lanes
                eye.wb[t] = wo;
                eye.flux[t] = beta;
                eye.coso[t] = cB;
                eye.isMed[t] = mediumEvent;
                eye.med[t] = fr.med;
                eye.k[t] = fr.k;
                eye.medID[t] = select(mediumEvent, ray.medium,
                                      UInt32P(LUX2_MEDIUM_CLEAR));
                eye.medFrontID[t] =
                    select(mediumEvent, ray.medium, hit.mediumFrontID);
                eye.medBackID[t] =
                    select(mediumEvent, ray.medium, hit.mediumBackID);
                eye.dARWeight[t] =
                    select(validVtx && (t >= 2 || ctx.useT1),
                           eye.pdfR[t - 1] * cB * divp(FloatP(1.f), d2),
                           FloatP(0.f));

                // No continuation sample at the last allowed vertex.
                if (t + 1 == maxV)
                    break;

                VertexP vtx;
                vtx.valid = validVtx;
                vtx.isMedium = mediumEvent;
                vtx.p = pNew;
                vtx.dg = dg; // zero normals on scatter lanes
                vtx.wo = wo;
                vtx.k = fr.k;
                vtx.bsdf = bsdf;
                vtx.med = fr.med;

                BSDFSampleP sample;
                // Scatter lanes reuse the u2bsdf dims.
                SampleVertex(vtx, st.sw, u2bsdf.x(), u2bsdf.y(), ucomp,
                             &sample, validVtx);
                const MaskP specular =
                    hitMask && has_flag(sample.sampledType, BSDFType::Specular);
                const MaskP sampOk = validVtx && (sample.pdf > FloatP(0.f));

                BSDFEvalP ev = ZeroEval();
                FloatP vtxCos(0.f); // cN carries the cos=1 override
                EvalVertex(vtx, st.sw, sample.wi, &ev, &vtxCos,
                           validVtx && !specular);

                // Lux SampleF outputs pdfR = p(wi|wo) (forward),
                // pdf = p(wo|wi) (reverse). Specular keeps lobe probability.
                // Scatter lanes: f = 1, pdf = pdfRev = phase.
                eye.pdfR[t] = sample.pdf;
                eye.pdf[t] = select(specular, sample.pdf, ev.pdfRev);
                eye.spec[t] = specular;
                const FloatP cN = select(mediumEvent, FloatP(1.f),
                                         abs(dot(sample.wi, dg.ng)));
                eye.cosi[t] = cN;
                // Lux vp.dAWeight = v.pdf * tPdf / vp.d2 * vp.cosi
                if (t >= 2)
                    eye.dAWeight[t - 1] =
                        select(sampOk,
                               eye.pdf[t] * eye.cosi[t - 1] *
                                   divp(FloatP(1.f), eye.d2[t - 1]),
                               eye.dAWeight[t - 1]);

                const FloatP f = sample.f;
                eye.rr[t] = min(FloatP(1.f),
                                max(ctx.lightRR, f * cB * divp(FloatP(1.f), cN)));
                eye.rrR[t] = min(FloatP(1.f), max(ctx.eyeRR, f));

                const SWCSpectrumP newBeta = beta * f;
                const MaskP rrActive = alive && ((t + 1) > RR_START);
                const MaskP kill = rrActive && (eye.rrR[t] < urr);
                beta = select(rrActive && !kill,
                              newBeta * divp(FloatP(1.f), eye.rrR[t]), newBeta);
                alive &= !(validVtx && !(sample.pdf > FloatP(0.f)));
                alive &= !kill;

                // Surface vertices offset along ng, scatter vertices step a
                // fixed epsilon along the sampled direction.
                const Point3fP surfSpawn =
                    OffsetRay(hit.p, hit.ngeo, sample.wi, FloatP(EPS_RAY));
                const Point3fP medSpawn = fr.p + sample.wi * FloatP(EPS_RAY);
                ray.o = select(alive,
                               select(mediumEvent, medSpawn, surfSpawn), ray.o);
                ray.d = select(alive, sample.wi, ray.d);
                ray.mint = FloatP(EPS_RAY);
                ray.maxt = FloatP(INF_F);
                ray.time = FloatP(0.f);
                ray.wavelengths = st.sw.w;
                ray.mask = UInt32P(0xFFFFFFFFu);
                if (ctx.hasMedia)
                    ray.medium = select(mediumEvent, ray.medium,
                                        select(hitMask,
                                               SwapMedium(hit, sample.wi),
                                               ray.medium));
                ray.UpdateReciprocalDirection();
            }
            *nEyeOut = nEye;
        }

        // ------------------------------------------------------------------
        // Light subpath
        // ------------------------------------------------------------------
        template <class LightT>
        void TraceLightFrom(const WalkCtx &ctx, const Sampler &sampler,
                            LaneState &st, Subpath &light, Int32P *nLightOut,
                            const LightT &selLight, const UInt32P &selIdx,
                            const FloatP &selPdf, int loBase, uint32_t slot)
        {
            const int maxV = ctx.lightDepth;
            const MaskP active = st.active;
            // Medium RNG base.
            const uint32_t K = 64u * uint32_t(ctx.eyeDepth + 1);
            const Int32P depthBase = Int32P(int(K) * int(1 + slot));

            const UInt32P lo = UInt32P(uint32_t(loBase));
            const Point2fP uPos =
                sampler.Get2D(st.seed, st.sampleIdx, lo + UInt32P(1u));
            const FloatP uComp0 =
                sampler.Get1D(st.seed, st.sampleIdx, lo + UInt32P(3u));
            const Point2fP uDir =
                sampler.Get2D(st.seed, st.sampleIdx, lo + UInt32P(4u));

            // Selection done by the caller.
            const UInt32P lightIdx = selIdx;
            light.selIdx = lightIdx;
            const MaskP la = active && (ctx.nLights > 0) && (maxV > 0);

            BSDFPtr ebsdf(nullptr);
            DifferentialGeometryP edg;
            FloatP pdfArea(0.f);
            SWCSpectrumP Le(0.f);
            const MaskP okOrigin = selLight->Sample_Le(
                st.sw, uPos.x(), uPos.y(), uComp0, &ebsdf, &edg, &pdfArea, &Le, la);

            const Vector3fP nVec(edg.n.x(), edg.n.y(), edg.n.z());
            BSDFSampleP s0;
            ebsdf->SampleF(st.sw, nVec, edg, uDir.x(), uDir.y(), FloatP(0.f), &s0,
                           TransportMode::Importance, okOrigin);
            const Vector3fP wi0 = s0.wi;
            const FloatP pdfDir = s0.pdf;
            const MaskP ok = okOrigin && (pdfDir > FloatP(0.f)) &&
                             (pdfArea > FloatP(0.f));

            // Lux light0.dAWeight = pdfArea * lPdf; Le /= lPdf.
            const FloatP aF0 = selPdf * pdfArea;
            const FloatP cos0 = abs(dot(wi0, edg.n));
            const FloatP f0 = cos0 * divp(FloatP(1.f), pdfDir);

            light.p[0] = edg.p;
            light.dg[0] = edg;
            light.bsdf[0] = ebsdf;
            light.wb[0] = nVec; // Lux light0.wi = normal
            light.flux[0] = select(ok, Le * divp(FloatP(1.f), aF0), SWCSpectrumP(0.f));
            light.dAWeight[0] = select(ok, aF0, FloatP(0.f));
            light.dARWeight[0] = light.dAWeight[0]; // overwritten when vertex 1 samples
            light.cosi[0] = abs(dot(nVec, edg.ng));
            light.coso[0] = abs(dot(wi0, edg.ng));
            light.pdf[0] = pdfDir;
            light.pdfR[0] = FloatP(0.f);
            light.spec[0] = MaskP(false);
            light.rrR[0] = min(FloatP(1.f),
                               max(ctx.eyeRR, f0 * light.cosi[0] *
                                                  divp(FloatP(1.f), light.coso[0])));
            light.rr[0] = min(FloatP(1.f), max(ctx.lightRR, f0));

            Int32P nLight = select(ok, Int32P(1), Int32P(0));
            MaskP alive = ok;
            RayP ray(edg.p, wi0, FloatP(EPS_RAY), FloatP(INF_F), FloatP(0.f));
            ray.wavelengths = st.sw.w;
            ray.mask = UInt32P(0xFFFFFFFFu);
            // Light origin medium.
            ray.medium = UInt32P(LUX2_MEDIUM_CLEAR);
            light.medID[0] = UInt32P(LUX2_MEDIUM_CLEAR);
            light.medFrontID[0] = UInt32P(LUX2_MEDIUM_CLEAR);
            light.medBackID[0] = UInt32P(LUX2_MEDIUM_CLEAR);
            SWCSpectrumP beta = light.flux[0] * f0;

            for (int s = 1; s < maxV; ++s)
            {
                if (!any(alive))
                    break;
                // Active lanes this light path bounce.
                OccupancyStep(enoki::count(alive));
                const UInt32P d0 = LightBounce(loBase, s);
                const Point2fP u2bsdf = sampler.Get2D(st.seed, st.sampleIdx, d0);
                const FloatP ucomp =
                    sampler.Get1D(st.seed, st.sampleIdx, d0 + UInt32P(2u));
                const FloatP urr =
                    sampler.Get1D(st.seed, st.sampleIdx, d0 + UInt32P(3u));

                HitP hit;
                FlightResult fr;
                IntersectCollapsed(
                    ctx, ray, alive, st, Int32P(s), depthBase, &beta, nullptr,
                    hit, fr, [](const HitP &, const Vector3fP &, const MaskP &) {});
                alive &= !fr.absorbed;
                // Scatter vertices survive the miss check.
                const MaskP mediumEvent = fr.mediumEvent && alive;
                const MaskP didHit = alive && hit.hit;
                alive &= (hit.hit || mediumEvent);
                if (!any(alive))
                    break;

                const MaskP hitMask = didHit && !mediumEvent;
                const DifferentialGeometryP dg = MakeDG(hit, ray.d, hitMask);
                const BSDFPtr bsdf = ctx.table->Gather(hit.matID, hitMask);
                const Vector3fP wi = -ray.d;  // Lux v.wi back toward light
                const MaskP validVtx = alive; // hit or scatter
                nLight = select(validVtx, nLight + Int32P(1), nLight);

                // cos = 1 at scatter vertices.
                const Point3fP pNew = select(mediumEvent, fr.p, hit.p);
                const Vector3fP dv = pNew - light.p[s - 1];
                const FloatP d2 = dot(dv, dv);
                const FloatP cB = select(mediumEvent, FloatP(1.f),
                                         abs(dot(wi, dg.ng))); // v.cosi
                light.d2[s - 1] = select(validVtx, d2, light.d2[s - 1]);
                light.p[s] = pNew;
                light.dg[s] = dg;
                light.bsdf[s] = bsdf; // null on scatter lanes
                light.wb[s] = wi;
                light.flux[s] = beta;
                light.cosi[s] = cB;
                light.isMed[s] = mediumEvent;
                light.med[s] = fr.med;
                light.k[s] = fr.k;
                light.medID[s] = select(mediumEvent, ray.medium,
                                        UInt32P(LUX2_MEDIUM_CLEAR));
                light.medFrontID[s] =
                    select(mediumEvent, ray.medium, hit.mediumFrontID);
                light.medBackID[s] =
                    select(mediumEvent, ray.medium, hit.mediumBackID);
                // Lux v.dAWeight = vp.pdf * vp.tPdf / vp.d2 * v.cosi
                light.dAWeight[s] =
                    select(validVtx,
                           light.pdf[s - 1] * cB * divp(FloatP(1.f), d2),
                           FloatP(0.f));

                if (s == 1)
                {
                    // Direct sampling of light[0] from vertex 1,
                    // area measure at light[0]. Vertex 1 may be a scatter.
                    const FloatP pD =
                        enoki::gather<FloatP>(ctx.lsD->pSel.data(), lightIdx,
                                              validVtx);
                    const FloatP pdfSolid =
                        selLight->Pdf_L(light.p[1], light.dg[1].n, light.p[0],
                                        light.dg[0].n, validVtx,
                                        !light.isMed[1]) *
                        pD;
                    light.directPdf1 =
                        ConvertDensity(light.p[1], pdfSolid, light.p[0], light.dg[0].ng);
                    if (ctx.checkDensity)
                    {
                        const FloatP area = enoki::gather<FloatP>(
                            ctx.areaByIdx.data(), lightIdx, validVtx);
                        BidirCheckAreaDensity("directPdf1", light.directPdf1,
                                              area, pD);
                    }
                }

                if (s + 1 == maxV)
                    break; // no sample at the last allowed vertex

                VertexP vtx;
                vtx.valid = validVtx;
                vtx.isMedium = mediumEvent;
                vtx.p = pNew;
                vtx.dg = dg; // zero normals on scatter lanes
                vtx.wo = wi;
                vtx.k = fr.k;
                vtx.bsdf = bsdf;
                vtx.med = fr.med;

                BSDFSampleP sample;
                SampleVertex(vtx, st.sw, u2bsdf.x(), u2bsdf.y(), ucomp, &sample,
                             validVtx, TransportMode::Importance);
                const MaskP specular =
                    hitMask && has_flag(sample.sampledType, BSDFType::Specular);
                const MaskP sampOk = validVtx && (sample.pdf > FloatP(0.f));

                BSDFEvalP ev = ZeroEval();
                FloatP vtxCos(0.f); // cN carries the cos=1 override
                EvalVertex(vtx, st.sw, sample.wi, &ev, &vtxCos,
                           validVtx && !specular, TransportMode::Importance);

                // Lux light SampleF: pdf = forward, pdfR = reverse.
                // Scatter lanes: f = 1, pdf = pdfRev = phase.
                light.pdf[s] = sample.pdf;
                light.pdfR[s] = select(specular, sample.pdf, ev.pdfRev);
                light.spec[s] = specular;
                const FloatP cN = select(mediumEvent, FloatP(1.f),
                                         abs(dot(sample.wi, dg.ng))); // v.coso
                light.coso[s] = cN;
                // Lux vp.dARWeight = v.pdfR * v.tPdfR / vp.d2 * vp.coso
                light.dARWeight[s - 1] =
                    select(sampOk,
                           light.pdfR[s] * light.coso[s - 1] *
                               divp(FloatP(1.f), light.d2[s - 1]),
                           light.dARWeight[s - 1]);

                const FloatP f = sample.f;
                light.rrR[s] = min(FloatP(1.f),
                                   max(ctx.eyeRR, f * cB * divp(FloatP(1.f), cN)));
                light.rr[s] = min(FloatP(1.f), max(ctx.lightRR, f));

                const SWCSpectrumP newBeta = beta * f;
                const MaskP rrActive = alive && ((s + 1) > RR_START);
                const MaskP kill = rrActive && (light.rr[s] < urr);
                beta = select(rrActive && !kill,
                              newBeta * divp(FloatP(1.f), light.rr[s]), newBeta);
                alive &= !(validVtx && !(sample.pdf > FloatP(0.f)));
                alive &= !kill;

                const Point3fP surfSpawn =
                    OffsetRay(hit.p, hit.ngeo, sample.wi, FloatP(EPS_RAY));
                const Point3fP medSpawn = fr.p + sample.wi * FloatP(EPS_RAY);
                ray.o = select(alive,
                               select(mediumEvent, medSpawn, surfSpawn), ray.o);
                ray.d = select(alive, sample.wi, ray.d);
                ray.mint = FloatP(EPS_RAY);
                ray.maxt = FloatP(INF_F);
                ray.wavelengths = st.sw.w;
                ray.mask = UInt32P(0xFFFFFFFFu);
                if (ctx.hasMedia)
                    ray.medium = select(mediumEvent, ray.medium,
                                        select(hitMask,
                                               SwapMedium(hit, sample.wi),
                                               ray.medium));
                ray.UpdateReciprocalDirection();
            }
            *nLightOut = nLight;
        }

        void TraceLight(const WalkCtx &ctx, const Sampler &sampler, LaneState &st,
                        Subpath &light, Int32P *nLightOut, uint32_t slot)
        {
            if (ctx.lsP->kind == LightsSamplingStrategy::StrategyKind::All)
            {
                // One light subpath per task slot.
                const uint32_t k = ctx.lsP->task[slot];
                const int loBase = AllLightBase(ctx, slot);
                // Guard the dim layout.
                assert(loBase != LightOriginBaseI(ctx.eyeDepth));
                for (uint32_t s = 0; s < ctx.lsP->TaskCount(); ++s)
                    if (s != slot)
                        assert(loBase != AllLightBase(ctx, s));
                // And the NEE "all" namespace must sit below this one.
                const uint32_t nT = ctx.lsP->TaskCount();
                assert(uint32_t(NEE_ALL_BASE +
                                (uint32_t(ctx.eyeDepth) * nT + nT) * 2u) <
                       LIGHT_ALL_BASE);
                TraceLightFrom(ctx, sampler, st, light, nLightOut,
                               ctx.lightPtrs[k], UInt32P(k),
                               FloatP(ctx.lsP->pSel[k]), loBase, slot);
            }
            else
            {
                const int loBase = LightOriginBaseI(ctx.eyeDepth);
                const MaskP la =
                    st.active && (ctx.nLights > 0) && (ctx.lightDepth > 0);
                const UInt32P lo = UInt32P(uint32_t(loBase));
                const FloatP uSel =
                    sampler.Get1D(st.seed, st.sampleIdx, lo);
                const LightPick pick = SampleLight(*ctx.lsP, uSel, la);
                const LightPtr sel = enoki::gather<LightPtr>(
                    ctx.lightPtrs.data(), pick.idx, la);
                TraceLightFrom(ctx, sampler, st, light, nLightOut, sel,
                               pick.idx, pick.pSel, loBase, 0u);
            }
        }

        // ------------------------------------------------------------------
        // Eye part of EvalPath
        // ------------------------------------------------------------------
        inline void FillEyeTransient(Transient &tr, const Subpath &eye, int ei,
                                     const BSDFEvalP &ef, const FloatP &ecosi,
                                     const FloatP &lpdf, const FloatP &d2,
                                     const WalkCtx &ctx)
        {
            const FloatP invD2 = divp(FloatP(1.f), d2);
            tr.eAF_last = eye.dARWeight[ei];
            // eyeV.dAWeight = lpdf * ltPdf / d2 * ecosi
            tr.eAR_last = lpdf * ecosi * invD2;
            // eye[nE-2].dAWeight = epdf * tPdf / eye[nE-2].d2 * eye[nE-2].cosi
            tr.eAR_prev = ef.pdfRev * eye.cosi[ei - 1] *
                          divp(FloatP(1.f), eye.d2[ei - 1]);
            // eyeV.rr (nEye > 1), eyeV.rrR
            const FloatP den = ecosi * ef.pdfRev;
            tr.eRR_last = select(den > FloatP(EPS_DENOM),
                                 min(FloatP(1.f),
                                     max(ctx.lightRR,
                                         ef.f * eye.coso[ei] * divp(FloatP(1.f), den))),
                                 FloatP(0.f));
            tr.eRRR_last = select(ef.pdf > FloatP(EPS_DENOM),
                                  min(FloatP(1.f),
                                      max(ctx.eyeRR, ef.f * divp(FloatP(1.f), ef.pdf))),
                                  FloatP(0.f));
        }

        // ------------------------------------------------------------------
        // GetDirectLight at eye vertex nE-1
        // ------------------------------------------------------------------
        void ConnectDirect(const WalkCtx &ctx, const Sampler &sampler,
                           LaneState &st, const Subpath &eye, Subpath &nee,
                           int nE, const Int32P &nEye)
        {
            if (!want(ctx.cfg, Strat::NEE, nE, /*nL*/ 1))
                return;
            const int ei = nE - 1;
            const MaskP base = st.active && (nEye >= Int32P(nE)) && (ctx.nLights > 0);
            if (!any(base))
                return;

            // One NEE connection for a single light.
            auto directOne = [&](const auto &light, const UInt32P &lightIdx,
                                 const UInt32P &shadowKey, const FloatP &pSel,
                                 const Point2fP &uPos, const FloatP &uComp)
            {
                Point3fP lightP(0.f);
                Vector3fP wiL(0.f);
                Normal3fP lightN(0.f);
                FloatP pdfPos(0.f);
                SWCSpectrumP Le(0.f);
                // The eye vertex may be a medium scatter.
                const MaskP recvHasN = !eye.isMed[ei];
                const MaskP sampleOk = light->Sample_Li(
                    st.sw, eye.p[ei], eye.dg[ei].n, uPos.x(), uPos.y(), uComp,
                    &lightP, &wiL, &lightN, &pdfPos, &Le, base, recvHasN);
                const FloatP dist = enoki::norm(lightP - eye.p[ei]);
                const FloatP d2 = dist * dist;
                const FloatP pdfL = pdfPos * pSel;
                const FloatP tmax =
                    dist - enoki::max(FloatP(1e-3f), FloatP(1e-3f) * dist);

                // Phase on scatter lanes.
                VertexP evx;
                evx.valid = base;
                evx.isMedium = eye.isMed[ei];
                evx.p = eye.p[ei];
                evx.dg = eye.dg[ei];
                evx.wo = eye.wb[ei];
                evx.k = eye.k[ei];
                evx.bsdf = eye.bsdf[ei];
                evx.med = eye.med[ei];
                BSDFEvalP ef = ZeroEval();
                FloatP ecosiV(0.f);
                EvalVertex(evx, st.sw, wiL, &ef, &ecosiV, sampleOk);
                // Density cosine = 1 at a scatter.
                const FloatP ecosi = select(eye.isMed[ei], FloatP(1.f),
                                            abs(dot(wiL, eye.dg[ei].ng)));
                const MaskP tryVis = sampleOk && (pdfL > FloatP(0.f)) &&
                                     (ef.f > SWCSpectrumP(0.f));
                if (none(tryVis))
                    return;
                // Scatter lanes have ng == 0.
                const Point3fP shadowO =
                    OffsetRay(eye.p[ei], eye.dg[ei].ng, wiL, FloatP(EPS_RAY));
                const UInt32P startMed = StartMedium(eye, ei, wiL);
                const SWCSpectrumP Tr = ConnectTr(
                    ctx, shadowO, wiL, startMed, tmax, st.sw, st, Int32P(nE),
                    shadowKey, SHADOW_SALT, tryVis);
                const MaskP cont = tryVis && (Tr > SWCSpectrumP(0.f));
                if (!any(cont))
                    return;

                // Lux Ld * lf * ef * flux / d2 for a diffuse emitter, times the
                // interval transmittance.
                const SWCSpectrumP Lc =
                    eye.flux[ei] * ef.f * Le * Tr * divp(FloatP(1.f), pdfL);

                const FloatP area =
                    enoki::gather<FloatP>(ctx.areaByIdx.data(), lightIdx, cont);
                // One sided emitter.
                const FloatP cosLraw = dot(-wiL, lightN);
                const FloatP cosL =
                    select(cosLraw > FloatP(0.f), cosLraw, FloatP(0.f));

                // Virtual light vertex dAWeight = emission area pdf * lsP selection.
                const FloatP pLP =
                    enoki::gather<FloatP>(ctx.lsP->pSel.data(), lightIdx, cont);
                nee.dAWeight[0] = pLP * divp(FloatP(1.f), area);

                Transient tr;
                tr.isLightDirect = true;
                tr.pdfLightDirect = ConvertDensity(eye.p[ei], pdfL, lightP, lightN);
                const FloatP lpdf = cosL * INVPI; // emitter pdf
                FillEyeTransient(tr, eye, ei, ef, ecosi, lpdf, d2, ctx);
                // lightV.dARWeight = epdfR * etPdfR / d2 * lcoso
                tr.lAR_last = ef.pdf * cosL * divp(FloatP(1.f), d2);
                tr.lRR_last = select(lpdf > FloatP(EPS_DENOM),
                                     min(FloatP(1.f),
                                         max(ctx.lightRR,
                                             cosL * divp(FloatP(1.f), lpdf))),
                                     FloatP(0.f));
                tr.lRRR_last = FloatP(1.f); // nLight == 1

                Breakdown bd;
                const FloatP w = divp(FloatP(1.f),
                                      WeightPath(ctx, eye, nE, nee, 1, tr,
                                                 ctx.diag ? &bd : nullptr));
                st.L = select(cont, st.L + Lc * w, st.L);
                BidirTallyGhost(ctx.diag, ef, sampleOk, cont, Lc, w,
                                eye.bsdf[ei]->flags(), nE, /*nL*/ 1);
                BidirRecordFirefly(ctx.diag, ctx.cfg.threshold, st.x, st.y, cont,
                                   Lc, w, d2, /*s*/ 1, nE, &bd);
            };

            if (ctx.lsD->kind == LightsSamplingStrategy::StrategyKind::All)
            {
                // One task per slot.
                const uint32_t nT = ctx.lsD->TaskCount();
                for (uint32_t s = 0; s < nT; ++s)
                {
                    const uint32_t k = ctx.lsD->task[s];
                    // Two dims per slot.
                    const UInt32P d =
                        UInt32P(NEE_ALL_BASE) +
                        (UInt32P(ei) * UInt32P(nT) + UInt32P(s)) * UInt32P(2u);
                    directOne(ctx.lightPtrs[k], UInt32P(k), UInt32P(s),
                              FloatP(ctx.lsD->pSel[k]),
                              sampler.Get2D(st.seed, st.sampleIdx, d),
                              sampler.Get1D(st.seed, st.sampleIdx, d + UInt32P(1u)));
                }
            }
            else
            {
                const int neeBase =
                    NeeBaseI(LightOriginBaseI(ctx.eyeDepth), ctx.lightDepth);
                const UInt32P nd = NeeDim(neeBase, ei);
                const FloatP uSel = sampler.Get1D(st.seed, st.sampleIdx, nd);
                const Point2fP uPos =
                    sampler.Get2D(st.seed, st.sampleIdx, nd + UInt32P(1u));
                const FloatP uComp =
                    sampler.Get1D(st.seed, st.sampleIdx, nd + UInt32P(3u));
                const LightPick pick = SampleLight(*ctx.lsD, uSel, base);
                directOne(enoki::gather<LightPtr>(ctx.lightPtrs.data(), pick.idx,
                                                  base),
                          pick.idx, pick.idx, pick.pSel, uPos, uComp);
            }
        }

        // ------------------------------------------------------------------
        // EvalPath for light subpath prefix nL >= 1
        // ------------------------------------------------------------------
        void ConnectGeneral(const WalkCtx &ctx, LaneState &st, const Subpath &eye,
                            const Subpath &light, int nE, int nL,
                            const Int32P &nEye, const Int32P &nLight,
                            uint32_t slot)
        {
            if (!want(ctx.cfg, nL == 1 ? Strat::Origin : Strat::General, nE, nL))
                return;
            const int ei = nE - 1;
            const int li = nL - 1;
            const MaskP base = st.active && (nEye >= Int32P(nE)) &&
                               (nLight >= Int32P(nL));
            if (!any(base))
                return;

            const Vector3fP dRaw = light.p[li] - eye.p[ei];
            const FloatP d2 = dot(dRaw, dRaw);
            const Vector3fP ewi = dRaw * enoki::sqrt(divp(FloatP(1.f), d2));
            const Vector3fP lwi = -ewi;

            // Phase on scatter lanes, BSDF on surface lanes.
            VertexP evx;
            evx.valid = base;
            evx.isMedium = eye.isMed[ei];
            evx.p = eye.p[ei];
            evx.dg = eye.dg[ei];
            evx.wo = eye.wb[ei];
            evx.k = eye.k[ei];
            evx.bsdf = eye.bsdf[ei];
            evx.med = eye.med[ei];
            BSDFEvalP ef = ZeroEval();
            FloatP ecosiV(0.f);
            EvalVertex(evx, st.sw, ewi, &ef, &ecosiV, base);

            // Origin uses the analytic emitter and deeper vertices
            // use the BSDF. lf.pdf = lpdf, lf.pdfRev = lpdfR.
            FloatP lfv, lpdf, lpdfR(0.f);
            BSDFEvalP lf = ZeroEval();
            if (nL == 1)
            {
                // Emission is one sided.
                const FloatP front = dot(lwi, light.dg[0].ng);
                lfv = select(front > FloatP(0.f),
                             abs(dot(lwi, light.dg[0].n)), FloatP(0.f));
                lpdf = lfv * INVPI;
            }
            else
            {
                // Lux EvalPath F(sw, lightV.wi /*back*/, lwo /*toward-eye*/, reverse=false).
                const MaskP lMed = light.isMed[li];
                light.bsdf[li]->Eval(st.sw, light.wb[li], lwi, light.dg[li],
                                     TransportMode::Importance, &lf,
                                     base && !lMed);
                const FloatP ph = select(
                    lMed,
                    light.med[li]->PhasePdf(st.sw, light.p[li], light.wb[li],
                                            lwi, base && lMed),
                    FloatP(0.f));
                lfv = select(lMed, ph, lf.f);
                lpdf = select(lMed, ph, lf.pdfRev);
                lpdfR = select(lMed, ph, lf.pdf);
            }

            const SWCSpectrumP Lc0 = light.flux[li] * lfv * ef.f * eye.flux[ei];
            const MaskP preCont = base && (d2 > FloatP(EPS_DENOM)) &&
                                  (Lc0 > SWCSpectrumP(0.f));
            if (!any(preCont))
                return;

            // Zero ng = no offset.
            const Point3fP shadowO =
                OffsetRay(eye.p[ei], eye.dg[ei].ng, ewi, FloatP(EPS_RAY));
            const FloatP dist = enoki::sqrt(d2);
            const FloatP tmax = dist - enoki::max(FloatP(1e-3f), FloatP(1e-3f) * dist);
            const UInt32P startMed = StartMedium(eye, ei, ewi);
            const uint32_t K = 64u * uint32_t(ctx.eyeDepth + 1);
            const SWCSpectrumP Tr =
                ConnectTr(ctx, shadowO, ewi, startMed, tmax, st.sw, st,
                          Int32P(nE),
                          UInt32P(uint32_t(slot * K + nE * 64 + nL)),
                          BDPT_CONN_SALT, preCont);
            const MaskP cont = preCont && (Tr > SWCSpectrumP(0.f));
            if (!any(cont))
                return;
            const SWCSpectrumP Lc = Lc0 * divp(FloatP(1.f), d2) * Tr;

            // Density cosines = 1 at a scatter endpoint.
            const FloatP ecosi = select(eye.isMed[ei], FloatP(1.f),
                                        abs(dot(ewi, eye.dg[ei].ng)));
            const FloatP lcoso = select(light.isMed[li], FloatP(1.f),
                                        abs(dot(lwi, light.dg[li].ng)));

            Transient tr;
            tr.isLightDirect = false;
            FillEyeTransient(tr, eye, ei, ef, ecosi, lpdf, d2, ctx);
            // lightV.dARWeight = epdfR * etPdfR / d2 * lcoso
            tr.lAR_last = ef.pdf * lcoso * divp(FloatP(1.f), d2);
            // light[nL-2].dARWeight = lpdfR * tPdfR / light[nL-2].d2 * light[nL-2].coso
            tr.lAR_prev = nL > 1 ? lpdfR * light.coso[li - 1] *
                                       divp(FloatP(1.f), light.d2[li - 1])
                                 : FloatP(0.f);
            tr.lRR_last = select(lpdf > FloatP(EPS_DENOM),
                                 min(FloatP(1.f),
                                     max(ctx.lightRR, lfv * divp(FloatP(1.f), lpdf))),
                                 FloatP(0.f));
            if (nL == 1)
                tr.lRRR_last = FloatP(1.f);
            else
            {
                const FloatP den = lcoso * lpdfR;
                tr.lRRR_last =
                    select(den > FloatP(EPS_DENOM),
                           min(FloatP(1.f),
                               max(ctx.eyeRR,
                                   lfv * light.cosi[li] * divp(FloatP(1.f), den))),
                           FloatP(0.f));
            }

            // pdfLightDirect: nL==1 -> direct sampling of light0 from the eye
            // vertex (light->Pdf(vE.p, light0) * lsD selection).
            if (nL == 1)
            {
                const LightPtr selLight = enoki::gather<LightPtr>(
                    ctx.lightPtrs.data(), light.selIdx, cont);
                const FloatP pD =
                    enoki::gather<FloatP>(ctx.lsD->pSel.data(), light.selIdx,
                                          cont);
                // Receiver = eye vertex ei; a scatter has no normal.
                const FloatP pdfSolid =
                    selLight->Pdf_L(eye.p[ei], eye.dg[ei].n, light.p[0],
                                    light.dg[0].n, cont, !eye.isMed[ei]) *
                    pD;
                tr.pdfLightDirect =
                    ConvertDensity(eye.p[ei], pdfSolid, light.p[0], light.dg[0].ng);
                if (ctx.checkDensity)
                {
                    const FloatP area = enoki::gather<FloatP>(
                        ctx.areaByIdx.data(), light.selIdx, cont);
                    BidirCheckAreaDensity("pdfLightDirect", tr.pdfLightDirect,
                                          area, pD);
                }
            }
            else
                tr.pdfLightDirect = light.directPdf1;

            Breakdown bd;
            const FloatP w = divp(FloatP(1.f),
                                  WeightPath(ctx, eye, nE, light, nL, tr,
                                             ctx.diag ? &bd : nullptr));
            st.L = select(cont, st.L + Lc * w, st.L);
            BidirTallyGhost(ctx.diag, ef, base, cont, Lc, w,
                            eye.bsdf[ei]->flags(), nE, nL);
            if (nL >= 2) // origin has no BSDF Eval, only deeper light vertices do
                BidirTallyGhost(ctx.diag, lf, base, cont, Lc, w,
                                light.bsdf[li]->flags(), nE, nL);
            BidirRecordFirefly(ctx.diag, ctx.cfg.threshold, st.x, st.y, cont,
                               Lc, w, d2, nL, nE, &bd);
        }

        // ------------------------------------------------------------------
        // Connect light subpath vertex to the camera
        // ------------------------------------------------------------------
        void ConnectCamera(const WalkCtx &ctx, LaneState &st,
                           const Subpath &eye, const Subpath &light,
                           int nL, const Int32P &nLight, Film &film,
                           uint32_t slot)
        {
            if (!want(ctx.cfg, nL == 1 ? Strat::Origin : Strat::General,
                      /*nE*/ 1, nL))
                return;
            const int li = nL - 1;
            const MaskP base = st.active && (nLight >= Int32P(nL));
            if (!any(base))
                return;

            const Vector3fP dRaw = light.p[li] - eye.p[0];
            const FloatP d2 = dot(dRaw, dRaw);
            const Vector3fP ewi = dRaw * enoki::sqrt(divp(FloatP(1.f), d2));
            const Vector3fP lwi = -ewi;

            FloatP px(0.f), py(0.f), We(0.f), cosCam(0.f);
            const MaskP inView =
                ctx.camera->Project(ewi, &px, &py, &We, &cosCam, base);
            if (!any(inView))
                return;

            FloatP lfv, lpdf, lpdfR(0.f);
            BSDFEvalP lf = ZeroEval();
            if (nL == 1)
            {
                const FloatP front = dot(lwi, light.dg[0].ng);
                lfv = select(front > FloatP(0.f),
                             abs(dot(lwi, light.dg[0].n)), FloatP(0.f));
                lpdf = lfv * INVPI;
            }
            else
            {
                // F(sw, lightV.wi /*back*/, lwo /*toward-eye*/, false).
                // Scatter lanes take the symmetric phase.
                const MaskP lMed = light.isMed[li];
                light.bsdf[li]->Eval(st.sw, light.wb[li], lwi, light.dg[li],
                                     TransportMode::Importance, &lf,
                                     inView && !lMed);
                const FloatP ph = select(
                    lMed,
                    light.med[li]->PhasePdf(st.sw, light.p[li], light.wb[li],
                                            lwi, inView && lMed),
                    FloatP(0.f));
                lfv = select(lMed, ph, lf.f);
                lpdf = select(lMed, ph, lf.pdfRev);
                lpdfR = select(lMed, ph, lf.pdf);
            }

            // Camera BSDF F = We (PerspectiveBSDF::F).
            const SWCSpectrumP Lc0 = light.flux[li] * lfv * We;
            const MaskP preCont = inView && (d2 > FloatP(EPS_DENOM)) &&
                                  (Lc0 > SWCSpectrumP(0.f));
            if (!any(preCont))
                return;

            const FloatP dist = enoki::sqrt(d2);
            const FloatP tmax =
                dist - enoki::max(FloatP(1e-3f), FloatP(1e-3f) * dist);
            const uint32_t K = 64u * uint32_t(ctx.eyeDepth + 1);
            const SWCSpectrumP Tr =
                ConnectTr(ctx, eye.p[0], ewi, ctx.cameraMediumID, tmax, st.sw,
                          st, Int32P(0),
                          UInt32P(uint32_t(slot * K + 1 * 64 + nL)),
                          BDPT_CONN_SALT, preCont);
            const MaskP cont = preCont && (Tr > SWCSpectrumP(0.f));
            if (!any(cont))
                return;
            const SWCSpectrumP Lc = Lc0 * divp(FloatP(1.f), d2) * Tr;

            // Density cosine = 1 at a scatter endpoint.
            const FloatP lcoso = select(light.isMed[li], FloatP(1.f),
                                        abs(dot(lwi, light.dg[li].ng)));

            Transient tr;
            tr.eAF_last = eye.pdfR[0] * cosCam * divp(FloatP(1.f), d2);
            tr.eAR_last = lpdf * cosCam * divp(FloatP(1.f), d2);
            tr.eAR_prev = FloatP(0.f);
            tr.eRR_last = FloatP(1.f);
            tr.eRRR_last = FloatP(1.f);
            tr.lAR_last = We * lcoso * divp(FloatP(1.f), d2);
            tr.lAR_prev = nL > 1
                              ? lpdfR * light.coso[li - 1] *
                                    divp(FloatP(1.f), light.d2[li - 1])
                              : FloatP(0.f);
            tr.lRR_last =
                select(lpdf > FloatP(EPS_DENOM),
                       min(FloatP(1.f),
                           max(ctx.lightRR, lfv * divp(FloatP(1.f), lpdf))),
                       FloatP(0.f));
            if (nL == 1)
                tr.lRRR_last = FloatP(1.f);
            else
            {
                const FloatP den = lcoso * lpdfR;
                tr.lRRR_last =
                    select(den > FloatP(EPS_DENOM),
                           min(FloatP(1.f),
                               max(ctx.eyeRR,
                                   lfv * light.cosi[li] *
                                       divp(FloatP(1.f), den))),
                           FloatP(0.f));
            }
            if (nL == 1)
            {
                const LightPtr selLight = enoki::gather<LightPtr>(
                    ctx.lightPtrs.data(), light.selIdx, cont);
                const FloatP pD =
                    enoki::gather<FloatP>(ctx.lsD->pSel.data(), light.selIdx,
                                          cont);
                const FloatP pdfSolid =
                    selLight->Pdf_L(eye.p[0], Normal3fP(0.f), light.p[0],
                                    light.dg[0].n, cont, MaskP(false)) *
                    pD;
                tr.pdfLightDirect = ConvertDensity(eye.p[0], pdfSolid,
                                                   light.p[0], light.dg[0].ng);
            }
            else
            {
                tr.pdfLightDirect = light.directPdf1;
            }
            tr.isLightDirect = false;

            Breakdown bd;
            const FloatP w = divp(FloatP(1.f),
                                  WeightPath(ctx, eye, 1, light, nL, tr,
                                             ctx.diag ? &bd : nullptr));
            film.SplatLight(px, py, Lc * w, st.sw, cont);
            if (nL >= 2)
                BidirTallyGhost(ctx.diag, lf, inView, cont, Lc, w,
                                light.bsdf[li]->flags(), /*nE*/ 1, nL);
            BidirRecordFirefly(ctx.diag, ctx.cfg.threshold, px, py, cont, Lc,
                               w, d2, nL, /*t*/ 1, &bd);
        }

        // ------------------------------------------------------------------
        // NEE at camera vertex
        // ------------------------------------------------------------------
        void ConnectCameraDirect(const WalkCtx &ctx, const Sampler &sampler,
                                 LaneState &st, const Subpath &eye,
                                 Subpath &nee, Film &film)
        {
            if (!want(ctx.cfg, Strat::NEE, /*nE*/ 1, /*nL*/ 1))
                return;
            const MaskP base = st.active && (ctx.nLights > 0);
            if (!any(base))
                return;

            // One camera NEE connection for a single light.
            auto directOne = [&](const auto &light, const UInt32P &lightIdx,
                                 const UInt32P &shadowKey, const FloatP &pSel,
                                 const Point2fP &uPos, const FloatP &uComp)
            {
                Point3fP lightP(0.f);
                Vector3fP wiL(0.f);
                Normal3fP lightN(0.f);
                FloatP pdfPos(0.f);
                SWCSpectrumP Le(0.f);
                const MaskP sampleOk = light->Sample_Li(
                    st.sw, eye.p[0], Normal3fP(0.f), uPos.x(), uPos.y(), uComp,
                    &lightP, &wiL, &lightN, &pdfPos, &Le, base, MaskP(false));
                const FloatP dist = enoki::norm(lightP - eye.p[0]);
                const FloatP d2 = dist * dist;
                const FloatP pdfL = pdfPos * pSel;
                const FloatP tmax =
                    dist - enoki::max(FloatP(1e-3f), FloatP(1e-3f) * dist);

                const Vector3fP ewi =
                    (lightP - eye.p[0]) * enoki::sqrt(divp(FloatP(1.f), d2));
                FloatP px(0.f), py(0.f), We(0.f), cosCam(0.f);
                const MaskP inView =
                    ctx.camera->Project(ewi, &px, &py, &We, &cosCam, sampleOk);

                const MaskP tryVis =
                    inView && (pdfL > FloatP(0.f)) && (We > FloatP(0.f));
                if (none(tryVis))
                    return;
                const SWCSpectrumP Tr = ConnectTr(
                    ctx, eye.p[0], ewi, ctx.cameraMediumID, tmax, st.sw, st,
                    Int32P(1), shadowKey, SHADOW_SALT, tryVis);
                const MaskP cont = tryVis && (Tr > SWCSpectrumP(0.f));
                if (!any(cont))
                    return;

                const SWCSpectrumP Lc = We * Le * Tr * divp(FloatP(1.f), pdfL);

                const FloatP cosLraw = dot(-wiL, lightN);
                const FloatP cosL =
                    select(cosLraw > FloatP(0.f), cosLraw, FloatP(0.f));
                const FloatP area =
                    enoki::gather<FloatP>(ctx.areaByIdx.data(), lightIdx, cont);

                // Virtual light vertex dAWeight = emission area pdf * lsP selection.
                const FloatP pLP =
                    enoki::gather<FloatP>(ctx.lsP->pSel.data(), lightIdx, cont);
                nee.dAWeight[0] = pLP * divp(FloatP(1.f), area);

                Transient tr;
                tr.isLightDirect = true;
                tr.pdfLightDirect = ConvertDensity(eye.p[0], pdfL, lightP, lightN);
                const FloatP lpdf = cosL * INVPI;
                tr.eAF_last = eye.pdfR[0] * cosCam * divp(FloatP(1.f), d2);
                tr.eAR_last = lpdf * cosCam * divp(FloatP(1.f), d2);
                tr.eAR_prev = FloatP(0.f);
                tr.eRR_last = FloatP(1.f);
                tr.eRRR_last = FloatP(1.f);
                tr.lAR_last = We * cosL * divp(FloatP(1.f), d2);
                tr.lRR_last =
                    select(lpdf > FloatP(EPS_DENOM),
                           min(FloatP(1.f),
                               max(ctx.lightRR,
                                   cosL * divp(FloatP(1.f), lpdf))),
                           FloatP(0.f));
                tr.lRRR_last = FloatP(1.f);

                Breakdown bd;
                const FloatP w = divp(FloatP(1.f),
                                      WeightPath(ctx, eye, 1, nee, 1, tr,
                                                 ctx.diag ? &bd : nullptr));
                film.SplatLight(px, py, Lc * w, st.sw, cont);
                BidirRecordFirefly(ctx.diag, ctx.cfg.threshold, px, py, cont, Lc,
                                   w, d2, /*s*/ 1, /*t*/ 1, &bd);
            };

            if (ctx.lsD->kind == LightsSamplingStrategy::StrategyKind::All)
            {
                // One task per slot.
                const uint32_t nT = ctx.lsD->TaskCount();
                for (uint32_t s = 0; s < nT; ++s)
                {
                    const uint32_t k = ctx.lsD->task[s];
                    // Two dims per slot.
                    const UInt32P d =
                        UInt32P(NEE_ALL_BASE) +
                        (UInt32P(s)) * UInt32P(2u);
                    directOne(ctx.lightPtrs[k], UInt32P(k), UInt32P(s),
                              FloatP(ctx.lsD->pSel[k]),
                              sampler.Get2D(st.seed, st.sampleIdx, d),
                              sampler.Get1D(st.seed, st.sampleIdx, d + UInt32P(1u)));
                }
            }
            else
            {
                const int camNeeBase = CamNeeBaseI(
                    NeeBaseI(LightOriginBaseI(ctx.eyeDepth), ctx.lightDepth),
                    ctx.eyeDepth);
                const UInt32P nd = UInt32P(uint32_t(camNeeBase));
                const FloatP uSel = sampler.Get1D(st.seed, st.sampleIdx, nd);
                const Point2fP uPos =
                    sampler.Get2D(st.seed, st.sampleIdx, nd + UInt32P(1u));
                const FloatP uComp =
                    sampler.Get1D(st.seed, st.sampleIdx, nd + UInt32P(3u));
                const LightPick pick = SampleLight(*ctx.lsD, uSel, base);
                directOne(enoki::gather<LightPtr>(ctx.lightPtrs.data(), pick.idx,
                                                  base),
                          pick.idx, pick.idx, pick.pSel, uPos, uComp);
            }
        }
    } // namespace

    BidirIntegrator::~BidirIntegrator() = default;

    void BidirIntegrator::Start(const Scene &scene)
    {
        m_scene = &scene;

        // Build the two light sampling tables.
        m_lsD.clear();
        m_lsP.clear();
        for (int g = 0; g < scene.LightGroupCount(); ++g)
        {
            m_lsD.push_back(CreateLightsSamplingStrategy(
                m_lightStrategy, scene.GroupLights(g), g, scene,
                LightRole::Direct));
            m_lsP.push_back(CreateLightsSamplingStrategy(
                m_lightPathStrategy, scene.GroupLights(g), g, scene,
                LightRole::LightPath));
        }

#ifdef LUX2_INTEGRATOR_DEBUG
        if (m_diagCfg.diag && !m_diagStore)
        {
            m_diagStore = std::make_unique<BidirDiagStore>();
            m_diagStore->Init(m_eyeDepth, m_lightDepth);
        }
#endif

        // TODO: handles finite area lights only.
        if (!m_warnedLights.exchange(true))
        {
            const int nGroups = scene.LightGroupCount();
            for (int g = 0; g < nGroups; ++g)
            {
                for (const Light *l : scene.GroupLights(g))
                {
                    if (!l)
                        continue;
                    if (l->IsInfinite())
                        LOG(LUX_WARNING, LUX_SYSTEM)
                            << "Bidirectional: environment/infinite lights are "
                               "unsupported (env eye hits and light subpaths from "
                               "env are not implemented); results will be biased.";
                    else if (l->TotalArea() <= 0.f)
                        LOG(LUX_WARNING, LUX_SYSTEM)
                            << "Bidirectional: non-area light (TotalArea()==0); "
                               "light subpaths through it are dropped and its NEE "
                               "weight is not MIS corrected.";
                }
            }
        }
    }

    void BidirIntegrator::End(const Scene &)
    {
#ifdef LUX2_INTEGRATOR_DEBUG
        if (m_diagStore)
            BidirEndDump(*m_diagStore, m_diagCfg.threshold);
#endif
    }

    void BidirIntegrator::RenderTile(const Scene &scene, const Tile &tile,
                                     Film &dest, Sampler &sampler,
                                     uint64_t sampleBasis, uint32_t spp)
    {
        const Camera &camera = scene.GetCamera();
        OccupancyBegin();

        WalkCtx ctx;
        ctx.scene = &scene;
        ctx.embree = scene.GetEmbree();
        ctx.table = &scene.GetBsdfTable();
        ctx.activeGroup = scene.GetActiveGroup();
        ctx.multiGroup = scene.LightGroupCount() > 1;
        ctx.lightPtrs = scene.GroupLights(ctx.activeGroup);
        ctx.nLights = static_cast<int>(ctx.lightPtrs.size());
        // Sampling tables indexed by active light group.
        LightsSamplingStrategy fallbackD;
        LightsSamplingStrategy fallbackP;
        if (ctx.activeGroup >= 0 &&
            ctx.activeGroup < static_cast<int>(m_lsD.size()))
        {
            ctx.lsD = &m_lsD[ctx.activeGroup];
            ctx.lsP = &m_lsP[ctx.activeGroup];
        }
        else
        {
            // Fallback for RenderTile without Start().
            fallbackD = CreateLightsSamplingStrategy(m_lightStrategy, ctx.lightPtrs,
                                                     ctx.activeGroup, scene,
                                                     LightRole::Direct);
            ctx.lsD = &fallbackD;
            fallbackP = CreateLightsSamplingStrategy(
                m_lightPathStrategy, ctx.lightPtrs, ctx.activeGroup, scene,
                LightRole::LightPath);
            ctx.lsP = &fallbackP;
        }
        const int nArea = scene.AreaLightCount();
        ctx.areaPtrs.reserve(nArea);
        ctx.areaById.resize(size_t(std::max(0, nArea)), 0.f);
        for (int a = 0; a < nArea; ++a)
        {
            const Light *al = scene.GetAreaLight(a);
            ctx.areaPtrs.push_back(al);
            ctx.areaById[size_t(a)] = al ? al->TotalArea() : 0.f;
        }
        if (ctx.multiGroup)
        {
            ctx.areaGroup.resize(nArea);
            for (int a = 0; a < nArea; ++a)
                ctx.areaGroup[a] = scene.AreaLightGroup(a);
        }
        ctx.areaByIdx.resize(size_t(std::max(0, ctx.nLights)), 0.f);
        for (int i = 0; i < ctx.nLights; ++i)
        {
            const Light *l = ctx.lightPtrs[size_t(i)];
            ctx.areaByIdx[size_t(i)] = (l && !l->IsInfinite()) ? l->TotalArea() : 0.f;
        }
        ctx.camera = &camera;
        ctx.mediumTable = &scene.GetMediumTable();
        ctx.hasMedia = scene.HasMedia();
        // Id 0 is the ambient composite.
        ctx.cameraMediumID = UInt32P(LUX2_MEDIUM_CLEAR);
        ctx.eyeDepth = m_eyeDepth;
        ctx.lightDepth = m_lightDepth;
        ctx.eyeRR = FloatP(m_eyeRRThreshold);
        ctx.lightRR = FloatP(m_lightRRThreshold);
        ctx.useT1 = m_useT1 && camera.SupportsProject();
        // Accumulators merged into the store at tile end.
        ctx.cfg = m_diagCfg;
#ifdef LUX2_INTEGRATOR_DEBUG
        // Only the first tile of a debug render runs the density check.
        ctx.checkDensity = m_debug && !m_densityChecked.exchange(true);
        BidirDiagLocal dl;
        if (m_diagStore)
        {
            dl.Init(m_eyeDepth, m_lightDepth);
            ctx.diag = &dl;
        }
#endif

        LaneState st;
        st.active = MaskP(false);
        JobCursor jobs(tile, spp);
        RetireBuffer retire;

        Subpath eye, light, nee;
        eye.Init(std::max(1, m_eyeDepth));
        light.Init(std::max(1, m_lightDepth));
        nee.Init(1);

        Refill(sampler, jobs, sampleBasis, st, 1);
        while (any(st.active))
        {
            Int32P nEye;
            TraceEye(ctx, camera, sampler, st, eye, &nEye); // also does s==0

            // Eye-only strategies run once. Light subpaths loop over slots:
            // one per task under lightpathstrategy "all", else a single pass.
            const int maxT = std::min(m_eyeDepth, int(enoki::hmax(nEye)));
            const uint32_t nSlots =
                (ctx.lsP->kind == LightsSamplingStrategy::StrategyKind::All)
                    ? ctx.lsP->TaskCount()
                    : 1u;
            for (uint32_t slot = 0; slot < nSlots; ++slot)
            {
                Int32P nLight;
                TraceLight(ctx, sampler, st, light, &nLight, slot);
                const int maxS =
                    std::min(m_lightDepth, int(enoki::hmax(nLight)));
                if (ctx.useT1)
                {
                    if (slot == 0)
                        ConnectCameraDirect(ctx, sampler, st, eye, nee,
                                            scene.GetFilm());
                    for (int s = 1; s <= maxS; ++s)
                        ConnectCamera(ctx, st, eye, light, s, nLight,
                                      scene.GetFilm(), slot);
                }
                for (int t = 2; t <= maxT; ++t)
                {
                    if (slot == 0)
                        ConnectDirect(ctx, sampler, st, eye, nee, t, nEye);
                    for (int s = 1; s <= maxS; ++s)
                        ConnectGeneral(ctx, st, eye, light, t, s, nEye, nLight,
                                       slot);
                }
            }

            st.L = select(enoki::isfinite(st.L), st.L, SWCSpectrumP(0.f));
            retire.Push(st, st.active);
            retire.Flush(dest, false);
            st.active = MaskP(false);
            Refill(sampler, jobs, sampleBasis, st);
        }
        retire.Flush(dest, true);
        OccupancyTileEnd("bidir", tile.x0, tile.y0, unsigned(PACKET_WIDTH));

#ifdef LUX2_INTEGRATOR_DEBUG
        // Merge this tile's diagnostics into the shared store.
        if (m_diagStore)
            BidirMergeTile(*m_diagStore, dl);
#endif
    }

    std::shared_ptr<SurfaceIntegrator> BidirIntegrator::CreateSurfaceIntegrator(
        const PluginContext &ctx)
    {
        ParamSet *p = ctx.params;
        const int eyeDepth = p ? p->FindOneInt("eyedepth", 8) : 8;
        const int lightDepth = p ? p->FindOneInt("lightdepth", 8) : 8;
        const int maxDepth = p ? p->FindOneInt("maxdepth", -1) : -1; // unused (legacy has none)
        const float eyeRR = p ? p->FindOneFloat("eyerrthreshold", 0.f) : 0.f;
        const float lightRR = p ? p->FindOneFloat("lightrrthreshold", 0.f) : 0.f;
        const bool useT1 = p ? p->FindOneBool("uset1", false) : false;
        const bool debug = p ? p->FindOneBool("debug", false) : false;
        DiagConfig cfg;
        cfg.onlyS = p ? p->FindOneInt("onlys", -1) : -1;
        cfg.onlyT = p ? p->FindOneInt("onlyt", -1) : -1;
        cfg.noMis = p ? p->FindOneBool("nomis", false) : false;
        cfg.misPower = p ? p->FindOneFloat("mispower", 2.f) : 2.f;
        cfg.diag = p ? p->FindOneBool("diag", false) : false;
        cfg.threshold = p ? p->FindOneFloat("diagthreshold", 0.f) : 0.f;
        const std::string lightStrategy =
            p ? p->FindOneString("lightstrategy", "one") : "one";
        const std::string lightPathStrategy =
            p ? p->FindOneString("lightpathstrategy", "one") : "one";
        if (p)
            p->EraseString("mismode");
        return std::make_shared<BidirIntegrator>(
            eyeDepth, lightDepth, maxDepth, eyeRR, lightRR, useT1, debug, cfg,
            lightStrategy, lightPathStrategy);
    }

    LUX2_REGISTER_SURFACE_INTEGRATOR(BidirIntegrator, "bidirectional");

} // namespace lux2