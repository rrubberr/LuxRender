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

#include "integrators/path.h"

#include "core/bsdf_call.h"
#include "core/bsdfptr_table.h"
#include "core/color.h"
#include "core/dynload.h"
#include "core/embree2.h"
#include "core/light.h"
#include "core/light_call.h"
#include "core/math.h"
#include "core/register.h"
#include "core/scene.h"

#include <enoki/array.h>

#include <cstdio>
#include <cstring>
#include <limits>

#ifdef PACKET_OCCUPANCY
#include <atomic>
#endif

namespace lux2
{
#ifdef PACKET_OCCUPANCY
    namespace
    {
        // Per worker occupancy accumulators.
        thread_local double g_occ_laneSum = 0.;
        thread_local double g_occ_iters = 0.;
        // Global cap on printed tile summaries to prevent log spam.
        std::atomic<long> g_occ_tileDumps{0};
        constexpr long OCC_MAX_DUMPS = 64;
    } // namespace
#endif

    namespace
    {
        // Power heuristic for combining two sampling strategies.
        inline FloatP misWeight(const FloatP &aIn, const FloatP &bIn)
        {
            const FloatP a = aIn * aIn;
            const FloatP b = bIn * bIn;
            return select(a > FloatP(0.f), a / (a + b), FloatP(0.f));
        }

        // Geometric offset.
        inline Point3fP OffsetRay(const Point3fP &p, const Normal3fP &ng,
                                  const Vector3fP &d, FloatP k)
        {
            const Normal3fP s = select(dot(d, ng) > FloatP(0.f), ng, -ng);
            return p + Vector3fP(s.x(), s.y(), s.z()) * k;
        }
    } // namespace

    void PathIntegrator::Start(const Scene &scene)
    {
        m_scene = &scene;
    }

    namespace
    {
        // Tile context for a compacted walk.
        struct WalkCtx
        {
            const Scene *scene;
            const EmbreeScene *embree;
            const BsdfPtrTable *table;
            const std::vector<std::shared_ptr<Light>> *lights;
            int nLights;
            FloatP invNLights;
            // Light pointers and infinite flags indexed by light id.
            std::vector<const Light *> lightPtrs;
            std::vector<float> lightInf;
            // Area light pointers indexed by light ID.
            std::vector<const Light *> areaPtrs;
            bool FULL_MIS, NEE, includeEnv, rrEfficiency;
            int maxDepth;
            FloatP rrProb, INF;
        };

        // Persistent path state per SIMD lane.
        struct PathLanes
        {
            RayP ray;
            SpectrumWavelengthsP sw;
            SWCSpectrumP L, throughput;
            FloatP alpha, prevPdf;
            Point3fP prevP;
            Normal3fP prevN;
            Int32P depth;
            MaskP alive, specularBounce;
            FloatP x, y, camWeight;  // film position and weight for retire
            UInt32P seed, sampleIdx; // sampler addressing
            SWCSpectrumP lem;        // emission on hit AOV accumulator
        };

        // Sample source adapter so MLT can reuse.
        struct SamplerSource
        {
            const Sampler &sampler;
            const UInt32P seed;
            const UInt32P sidx;
            FloatP Get1D(const UInt32P &dim) const
            {
                return sampler.Get1D(seed, sidx, dim);
            }
            Point2fP Get2D(const UInt32P &dim) const
            {
                return sampler.Get2D(seed, sidx, dim);
            }
        };

        // Advance every live lane by one bounce.
        template <class SampleSourceT>
        void StepBounce(const WalkCtx &ctx, const SampleSourceT &src,
                        PathLanes &st)
        {
            const EmbreeScene *embree = ctx.embree;
            const BsdfPtrTable &table = *ctx.table;
            const std::vector<std::shared_ptr<Light>> &lights = *ctx.lights;
            const int nLights = ctx.nLights;
            const FloatP invNLights = ctx.invNLights;
            const bool FULL_MIS = ctx.FULL_MIS;
            const bool NEE = ctx.NEE;
            const bool includeEnv = ctx.includeEnv;
            const int maxDepth = ctx.maxDepth;
            const bool rrEfficiency = ctx.rrEfficiency;
            const FloatP rrProb = ctx.rrProb;
            const FloatP INF = ctx.INF;
            const Scene &scene = *ctx.scene;

            RayP &ray = st.ray;
            SWCSpectrumP &L = st.L;
            SWCSpectrumP &throughput = st.throughput;
            FloatP &alpha = st.alpha;
            Int32P &depth = st.depth;
            MaskP &specularBounce = st.specularBounce;
            MaskP &alive = st.alive;
            Point3fP &prevP = st.prevP;
            Normal3fP &prevN = st.prevN;
            FloatP &prevPdf = st.prevPdf;
            const SpectrumWavelengthsP &sw = st.sw;

            // Lane stream base.
            const UInt32P d0 =
                UInt32P(2u) + UInt32P(depth) * UInt32P(6u);
            const Point2fP u2bsdf = src.Get2D(d0);
            const FloatP ucomp = src.Get1D(d0 + UInt32P(1u));
            const FloatP urr = src.Get1D(d0 + UInt32P(2u));
            const FloatP ulight = src.Get1D(d0 + UInt32P(3u));
            const Point2fP u2lpos = src.Get2D(d0 + UInt32P(4u));
            const FloatP ulightcomp = src.Get1D(d0 + UInt32P(5u));

            ray.alive = alive;
            HitP hit;
            embree->Intersect(ray, hit, Coherent::No);

            // Environment / infinite light.
            const MaskP miss = alive && !hit.hit;
            if (any(miss))
            {
                // Broadcast the scalar params to lane masks.
                const MaskP envOn = MaskP(includeEnv);
                const MaskP deepper = depth > Int32P(0);
                MaskP envCount;
                if (FULL_MIS)
                    envCount = miss && (envOn || deepper);
                else
                    envCount = miss && (MaskP(!NEE) || ((envOn || deepper) && specularBounce));
                if (nLights > 0 && any(envCount))
                {
                    for (int k = 0; k < nLights; ++k)
                    {
                        const Light *light = lights[k].get();
                        if (!light->IsInfinite())
                            continue;
                        const MaskP la = envCount;
                        if (none(la))
                            continue;
                        FloatP envW(1.f);
                        if (FULL_MIS && NEE)
                        {
                            const FloatP pdfLdir =
                                light->Pdf_L(prevP, prevN, Point3fP(0.f),
                                             Normal3fP(0.f), la) *
                                invNLights;
                            envW = select(specularBounce || eq(depth, Int32P(0)),
                                          FloatP(1.f), misWeight(prevPdf, pdfLdir));
                        }
                        L = select(la, L + envW * throughput * light->Le(ray, la), L);
                    }
                }
                // Escaped to the background.
                alpha = select(miss && eq(depth, Int32P(0)), FloatP(0.f), alpha);
                alive &= !miss;
            }

            // Hit!
            const MaskP hitMask = alive && hit.hit;
            if (any(hitMask))
            {
                // A primary ray that lands on geometry is opaque coverage.
                alpha = select(hitMask && eq(depth, Int32P(0)), FloatP(1.f), alpha);

                const Point3fP p = hit.p;
                const Normal3fP ng = hit.ngeo;
                const MaskP entering = dot(ray.d, ng) < FloatP(0.f);
                // lux never flips the shading normal to face the ray, BxDFs
                // work in a frame built on dgShading.nn and side logic runs
                // through ng.
                const Normal3fP n = hit.sh_n;

                DifferentialGeometryP dg;
                dg.p = p;
                dg.n = n;
                dg.ng = ng;
                dg.entering = entering;
                dg.uv_u = hit.uv.x();
                dg.uv_v = hit.uv.y();
                // Shading frame from the ShadeHit UV gradient solve.
                dg.dp_du = hit.dp_du;
                dg.dp_dv = hit.dp_dv;
                dg.dp_ds = hit.dp_ds;
                dg.dp_dt = hit.dp_dt;

                const Vector3fP wo = -ray.d;
                const BSDFPtr bsdf = table.Gather(hit.matID, hitMask);

                // Emission on hit.
                MaskP emCount = hitMask && (hit.lightID >= Int32P(0));
                if (!FULL_MIS && NEE)
                    emCount = emCount && specularBounce;
                SWCSpectrumP emTerm(0.f); // total emission added this bounce
                if (any(emCount))
                {
                    // One area light per lane selected by id.
                    const MaskP la = emCount;
                    const LightPtr al =
                        enoki::gather<LightPtr>(ctx.areaPtrs.data(), hit.lightID, la);
                    FloatP emW(1.f);
                    if (FULL_MIS && NEE)
                    {
                        // Shading normal n.
                        const FloatP pdfLEmitter =
                            al->Pdf_L(prevP, prevN, p, n, la) * invNLights;
                        emW = select(specularBounce, FloatP(1.f),
                                     misWeight(prevPdf, pdfLEmitter));
                    }
                    const SWCSpectrumP LeRaw = al->Le(dg, sw, wo, la);
                    const SWCSpectrumP Lem = emW * throughput * LeRaw;
                    emTerm = emTerm + select(la, Lem, SWCSpectrumP(0.f));
                    L = select(la, L + Lem, L);
                }
                {
                    // Visible emission only AOV.
                    const MaskP lemMask = emCount && eq(depth, Int32P(0));
                    st.lem = select(lemMask, st.lem + emTerm, st.lem);
                }

                // Depth limit.
                alive &= !(hitMask && depth >= Int32P(maxDepth));
                const MaskP hitAlive = hitMask && alive;

                // NEE. One light per lane.
                if (NEE && nLights > 0 && any(hitAlive))
                {
                    const UInt32P lightIdx =
                        min(UInt32P(uint32_t(nLights - 1)),
                            floor2int<UInt32P>(ulight * FloatP(float(nLights))));
                    const MaskP la = hitAlive;
                    const LightPtr light =
                        enoki::gather<LightPtr>(ctx.lightPtrs.data(), lightIdx, la);
                    const FloatP infF =
                        enoki::gather<FloatP>(ctx.lightInf.data(), lightIdx, la);

                    Point3fP lightP(0.f, 0.f, 0.f);
                    Vector3fP wi(0.f, 0.f, 0.f);
                    Normal3fP lightN(0.f, 0.f, 0.f);
                    FloatP pdfPos(0.f);
                    SWCSpectrumP Le(0.f);
                    const MaskP valid = light->Sample_L(
                        sw, p, n, u2lpos.x(), u2lpos.y(), ulightcomp, &lightP,
                        &wi, &lightN, &pdfPos, &Le, la);
                    const FloatP dist = enoki::norm(lightP - p);
                    const FloatP pdfL = pdfPos * invNLights;
                    // Offset toward the side the path arrived on.
                    const Point3fP off = OffsetRay(p, ng, -ray.d, FloatP(EPS_RAY));
                    // Stop short of a finite emitter with an absolute margin
                    // because a relative shrink can still clip its surface.
                    const FloatP tmax =
                        select(infF > FloatP(0.5f), INF,
                               dist - enoki::max(FloatP(1e-3f),
                                                 FloatP(1e-3f) * dist));
                    RayP shadow(off, wi, FloatP(EPS_RAY), tmax, FloatP(0.f));
                    shadow.wavelengths = sw.w;
                    shadow.mask = UInt32P(0xFFFFFFFFu);
                    const MaskP tryVis = la && valid && (pdfL > FloatP(0.f));
                    const MaskP vis =
                        tryVis && !embree->Occluded(shadow, tryVis, Coherent::No);

                    BSDFEvalP ev;
                    ev.f = SWCSpectrumP(0.f);
                    ev.pdf = FloatP(0.f);
                    ev.pdfRev = FloatP(0.f);
                    // Radiance == reverse (wo toward the eye), no ng
                    // Jacobian in Eval.f.
                    bsdf->Eval(sw, wi, wo, dg, TransportMode::Radiance, &ev, la);

                    // ev.f carries |cos(0ᵢ)|. cosL is hemisphere agnostic.
                    // Degenerate (wi,wo) pairs are zeroed in ev.f.
                    const FloatP cosL = abs(dot(wi, n));
                    FloatP neeW(1.f);
                    if (FULL_MIS)
                        neeW = misWeight(pdfL, ev.pdf);
                    const MaskP ok = vis && (pdfL > FloatP(0.f)) && (cosL > FloatP(0.f));
                    const SWCSpectrumP Ld = select(
                        ok, neeW * throughput * Le * ev.f / max(pdfL, FloatP(EPS_DENOM)),
                        SWCSpectrumP(0.f));
                    L = L + Ld;
                }

                // BSDF sample. Eye path uses reverse=true == TransportMode::Radiance.
                BSDFSampleP sample;
                sample.wi = Vector3fP(0.f);
                sample.f = SWCSpectrumP(0.f);
                sample.pdf = FloatP(0.f);
                sample.eta = FloatP(1.f);
                sample.sampledType = UInt32P(0u);
                sample.specular = MaskP(false);
                bsdf->SampleF(sw, wo, dg, u2bsdf.x(), u2bsdf.y(), ucomp, &sample,
                              TransportMode::Radiance, hitAlive);

                alive &= !(hitAlive && (sample.pdf <= FloatP(0.f)));

                // Lux applies efficiency RR to specular transmission bounces.
                const MaskP rrActive =
                    alive && (depth > Int32P(3));
                if (any(rrActive))
                {
                    const SWCSpectrumP tputF = sample.f;
                    FloatP q;
                    if (rrEfficiency)
                        q = min(FloatP(1.f), max(tputF, FloatP(0.f)));
                    else
                        q = rrProb;
                    const MaskP kill = rrActive && (q < urr);
                    throughput = select(rrActive && !kill,
                                        throughput / max(q, FloatP(EPS_DENOM)),
                                        throughput);
                    alive &= !kill;
                }

                // Advance.
                throughput = select(alive, throughput * sample.f, throughput);
                specularBounce =
                    select(hitMask, has_flag(sample.sampledType, BSDFType::Specular),
                           specularBounce);

                const Point3fP spawn = OffsetRay(p, ng, sample.wi, FloatP(EPS_RAY));
                ray = RayP(spawn, sample.wi, FloatP(EPS_RAY), INF, FloatP(0.f));
                ray.wavelengths = sw.w;
                ray.mask = UInt32P(0xFFFFFFFFu);
                depth = depth + Int32P(1);
                prevP = p;
                prevN = n;
                prevPdf = sample.pdf;
            }
        }

        // Pixel sample handed to each lane.
        struct Job
        {
            int px, py;
            uint32_t s;
        };

        // Enumerates (px, py, s) jobs over tiles in scanline order.
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

        // Fill dead lanes with the next jobs.
        template <class SamplerT>
        void Refill(const Camera &camera, const SamplerT &sampler,
                    JobCursor &jobs, uint64_t sampleBasis, PathLanes &st,
                    int k = 2)
        {
            const int nDead = enoki::count(!st.alive);
            if (nDead < k)
                return;

            // Read lane state as floats.
            alignas(64) float aliveF[PACKET_WIDTH];
            enoki::store_unaligned(aliveF, select(st.alive, FloatP(1.f), FloatP(0.f)));

            alignas(64) float fillF[PACKET_WIDTH] = {};
            alignas(64) float pxs[PACKET_WIDTH] = {}, pys[PACKET_WIDTH] = {};
            alignas(64) uint32_t seeds[PACKET_WIDTH] = {}, sidxs[PACKET_WIDTH] = {};

            // Fold the sampler's configured base seed.
            const uint64_t baseSeed = sampler.BaseSeed();

            bool anyFill = false;
            for (size_t i = 0; i < PACKET_WIDTH; ++i)
            {
                if (aliveF[i] > 0.f)
                    continue; // lane still live
                Job j;
                if (!jobs.Next(j))
                    break; // jobs exhausted
                fillF[i] = 1.f;
                pxs[i] = float(j.px);
                pys[i] = float(j.py);
                sidxs[i] = j.s;
                // Scramble.
                const uint64_t h =
                    ((sampleBasis ^ baseSeed) * 0x9E3779B97F4A7C15ull) ^
                    (uint64_t(j.px) * 0xBF58476D1CE4E5B9ull) ^
                    (uint64_t(j.py) * 0x94D049BB133111EBull);
                seeds[i] = uint32_t(h >> 32);
                anyFill = true;
            }
            if (!anyFill)
                return;

            // fillF is 1.0/0.0 floats; derive the mask from a comparison.
            const MaskP fillMask =
                enoki::load_unaligned<FloatP>(fillF) > FloatP(0.5f);

            const UInt32P vseed = enoki::load_unaligned<UInt32P>(seeds);
            const UInt32P vidx = enoki::load_unaligned<UInt32P>(sidxs);
            const FloatP pxf = enoki::load_unaligned<FloatP>(pxs);
            const FloatP pyf = enoki::load_unaligned<FloatP>(pys);

            // Primary sample streams.
            const Point2fP jit = sampler.Get2D(vseed, vidx, UInt32P(0u));
            SpectrumWavelengthsP newSw;
            newSw.Sample(sampler.Get1D(vseed, vidx, UInt32P(1u)));

            const FloatP x = pxf + jit.x();
            const FloatP y = pyf + jit.y();

            RayP cam;
            FloatP w;
            camera.GenerateRay(x, y, FloatP(0.f), &cam, &w);
            cam.wavelengths = newSw.w;
            cam.mask = UInt32P(0xFFFFFFFFu);

            // Fully reset a lane.
            st.ray.o = select(fillMask, cam.o, st.ray.o);
            st.ray.d = select(fillMask, cam.d, st.ray.d);
            st.ray.mint = select(fillMask, cam.mint, st.ray.mint);
            st.ray.maxt = select(fillMask, cam.maxt, st.ray.maxt);
            st.ray.time = select(fillMask, cam.time, st.ray.time);
            st.ray.wavelengths =
                select(fillMask, cam.wavelengths, st.ray.wavelengths);
            st.ray.mask = select(fillMask, cam.mask, st.ray.mask);

            st.sw.w = select(fillMask, newSw.w, st.sw.w);
            st.sw.binRGB = select(fillMask, newSw.binRGB, st.sw.binRGB);
            st.sw.offsetRGB = select(fillMask, newSw.offsetRGB, st.sw.offsetRGB);
            st.sw.binXYZ = select(fillMask, newSw.binXYZ, st.sw.binXYZ);
            st.sw.offsetXYZ = select(fillMask, newSw.offsetXYZ, st.sw.offsetXYZ);

            st.L = select(fillMask, SWCSpectrumP(0.f), st.L);
            st.throughput = select(fillMask, SWCSpectrumP(1.f), st.throughput);
            st.alpha = select(fillMask, FloatP(0.f), st.alpha);
            st.depth = select(fillMask, Int32P(0), st.depth);
            // First bounce is treated as specular for MIS weighting.
            st.specularBounce = select(fillMask, MaskP(true), st.specularBounce);
            st.prevP = select(fillMask, cam.o, st.prevP);
            st.prevN = select(fillMask, Normal3fP(0.f, 0.f, 0.f), st.prevN);
            st.prevPdf = select(fillMask, FloatP(0.f), st.prevPdf);
            st.lem = select(fillMask, SWCSpectrumP(0.f), st.lem);

            st.x = select(fillMask, x, st.x);
            st.y = select(fillMask, y, st.y);
            st.camWeight = select(fillMask, w, st.camWeight);
            st.seed = select(fillMask, vseed, st.seed);
            st.sampleIdx = select(fillMask, vidx, st.sampleIdx);

            // Recompute the reciprocal direction for filled lanes.
            const Vector3fP newRcp = enoki::rcp(st.ray.d);
            st.ray.d_rcp = select(fillMask, newRcp, st.ray.d_rcp);

            st.alive = st.alive || fillMask;
        }

        // Batch retired paths so Push can compact finished lanes with
        // enoki::compress and Flush can load whole packets.
        struct RetireBuffer
        {
            // compress() writes packet granularly with n <= 2W-1 pending
            // and up to W new entries.
            static constexpr size_t CAP = 3 * PACKET_WIDTH;
            float bx[CAP], by[CAP], bL[CAP], bwl[CAP], ba[CAP], bwt[CAP];
            size_t n = 0;

            void Push(const PathLanes &st, const MaskP &finished)
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

            // Emit whole packets while they accumulate.
            void Flush(Film &dest, bool all)
            {
                while (n >= PACKET_WIDTH || (all && n > 0))
                {
                    const size_t take =
                        n < PACKET_WIDTH ? n : size_t(PACKET_WIDTH);
                    // Tail lanes contribute nothing.
                    const MaskP real =
                        enoki::arange<FloatP>() < FloatP(float(take));
                    auto loadMasked = [&](const float *b)
                    {
                        return select(real, enoki::load_unaligned<FloatP>(b),
                                      FloatP(0.f));
                    };
                    SpectrumWavelengthsP sw;
                    sw.FromWavelength(loadMasked(bwl));
                    dest.Splat(loadMasked(bx), loadMasked(by),
                               SWCSpectrumP(loadMasked(bL)), sw,
                               loadMasked(ba), loadMasked(bwt), 0);
                    // Compact the remainder to the front.
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
    } // namespace

    void PathIntegrator::RenderTile(const Scene &scene, const Tile &tile,
                                    Film &dest, Sampler &sampler,
                                    uint64_t sampleBasis, uint32_t spp)
    {
        const Camera &camera = scene.GetCamera();

#ifdef PACKET_OCCUPANCY
        // The summary reflects this tile's paths only.
        g_occ_laneSum = 0.;
        g_occ_iters = 0.;
#endif

        // Tile constant config.
        WalkCtx ctx;
        ctx.scene = &scene;
        ctx.embree = scene.GetEmbree();
        ctx.table = &scene.GetBsdfTable();
        ctx.lights = &scene.GetLights();
        ctx.nLights = static_cast<int>(ctx.lights->size());
        ctx.invNLights =
            ctx.nLights > 0 ? FloatP(1.f / float(ctx.nLights)) : FloatP(0.f);
        ctx.lightPtrs.reserve(ctx.nLights);
        ctx.lightInf.reserve(ctx.nLights);
        for (const auto &l : *ctx.lights)
        {
            ctx.lightPtrs.push_back(l.get());
            ctx.lightInf.push_back(l->IsInfinite() ? 1.f : 0.f);
        }
        const int nArea = scene.AreaLightCount();
        ctx.areaPtrs.reserve(nArea);
        for (int a = 0; a < nArea; ++a)
            ctx.areaPtrs.push_back(scene.GetAreaLight(a));
        ctx.FULL_MIS = (m_lightMode == LightMode::MIS);
        ctx.NEE = (m_lightMode != LightMode::BSDF) && m_directLightSampling;
        ctx.includeEnv = m_includeEnvironment;
        ctx.maxDepth = m_maxDepth;
        ctx.rrEfficiency = (m_rrStrategy == "efficiency");
        ctx.rrProb = FloatP(m_rrContinueProb);
        ctx.INF = std::numeric_limits<float>::infinity();

        // All lanes start dead.
        PathLanes st;
        st.alive = MaskP(false);
        JobCursor jobs(tile, spp);
        RetireBuffer retire;

        Refill(camera, sampler, jobs, sampleBasis, st, 1);
        while (any(st.alive))
        {
#ifdef PACKET_OCCUPANCY
            // Active lanes this bounce.
            g_occ_laneSum += double(enoki::count(st.alive));
            g_occ_iters += 1.;
#endif
            const MaskP before = st.alive;
            StepBounce(ctx, SamplerSource{sampler, st.seed, st.sampleIdx}, st);

            // Paths that retired this bounce.
            const MaskP finished = before && !st.alive;
            if (any(finished))
            {
                retire.Push(st, finished);
                retire.Flush(dest, false);
            }
            // Refill the packet from the job queue.
            Refill(camera, sampler, jobs, sampleBasis, st);
        }
        retire.Flush(dest, true);

#ifdef PACKET_OCCUPANCY
        // Mean live lanes per bounce for this tile.
        if (g_occ_iters > 0.)
        {
            const long dumpIdx =
                g_occ_tileDumps.fetch_add(1, std::memory_order_relaxed);
            if (dumpIdx < OCC_MAX_DUMPS)
            {
                fprintf(stderr,
                        "[OCCUPANCY tile(%d,%d)] meanLanes=%.2f/%u "
                        "occupancy=%.1f%% iters=%.0f\n",
                        tile.x0, tile.y0, g_occ_laneSum / g_occ_iters,
                        unsigned(PACKET_WIDTH),
                        100. * (g_occ_laneSum / g_occ_iters) / double(PACKET_WIDTH),
                        g_occ_iters);
                if (dumpIdx == OCC_MAX_DUMPS - 1)
                    fprintf(stderr,
                            "[OCCUPANCY] summary cap (%ld) reached; "
                            "suppressing further tile summaries\n",
                            OCC_MAX_DUMPS);
            }
        }
#endif
    }

    std::shared_ptr<SurfaceIntegrator> PathIntegrator::CreateSurfaceIntegrator(
        const PluginContext &ctx)
    {
        ParamSet *p = ctx.params;
        const int maxDepth = p ? p->FindOneInt("maxdepth", 8) : 8;
        const float rrContinueProb =
            p ? p->FindOneFloat("rrcontinueprob", 0.65f) : 0.65f;
        const std::string rrStrategy =
            p ? p->FindOneString("rrstrategy", "efficiency") : "efficiency";
        const bool includeEnvironment =
            p ? p->FindOneBool("includeenvironment", true) : true;
        const bool directLightSampling =
            p ? p->FindOneBool("directlightsampling", true) : true;
        // Debug estimator selection: "mis" (default), "nee", or "bsdf".
        const std::string lightModeStr =
            p ? p->FindOneString("lightmode", "mis") : "mis";
        PathIntegrator::LightMode lightMode = PathIntegrator::LightMode::MIS;
        if (lightModeStr == "nee")
            lightMode = PathIntegrator::LightMode::NEE;
        else if (lightModeStr == "bsdf")
            lightMode = PathIntegrator::LightMode::BSDF;
        // "lightstrategy" is accepted and ignored.
        if (p)
            p->EraseString("lightstrategy");

        return std::make_shared<PathIntegrator>(
            maxDepth, rrContinueProb, rrStrategy,
            includeEnvironment, directLightSampling, lightMode);
    }

    LUX2_REGISTER_SURFACE_INTEGRATOR(PathIntegrator, "path");

} // namespace lux2
