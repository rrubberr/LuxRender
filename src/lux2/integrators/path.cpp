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

#include "integrators/debug/integrator_debug.h"
#include "integrators/path.h"
#include "integrators/core/vertex.h"

#include "core/bsdf_call.h"
#include "core/bsdfptr_table.h"
#include "core/color/color.h"
#include "core/dynload.h"
#include "core/accel/embree2.h"
#include "core/light.h"
#include "core/light_call.h"
#include "core/math/math.h"
#include "core/medium.h"
#include "core/mediumptr_table.h"
#include "core/phase.h"
#include "core/register.h"
#include "core/scene.h"
#include "core/rng.h"

#include <enoki/array.h>

#include <cstring>
#include <limits>

namespace lux2
{
    namespace
    {
        // Power heuristic for combining two sampling strategies.
        inline FloatP misWeight(const FloatP &aIn, const FloatP &bIn)
        {
            const FloatP a = aIn * aIn;
            const FloatP b = bIn * bIn;
            return select(a > FloatP(0.f), a / (a + b), FloatP(0.f));
        }

        // `All` strategy NEE slots draw from a high base so they
        // never collide with the regular stride.
        constexpr uint32_t NEE_ALL_DIM_BASE = 1u << 20;

        // Geometric offset.
        inline Point3fP OffsetRay(const Point3fP &p, const Normal3fP &ng,
                                  const Vector3fP &d, FloatP k)
        {
            const Normal3fP s = select(dot(d, ng) > FloatP(0.f), ng, -ng);
            return p + Vector3fP(s.x(), s.y(), s.z()) * k;
        }

        // Medium swap at a boundary.
        inline UInt32P SwapMedium(const HitP &hit, const Vector3fP &dir)
        {
            const MaskP leavesFront = dot(dir, hit.ngeo) > FloatP(0.f);
            return select(leavesFront, hit.mediumFrontID, hit.mediumBackID);
        }
    } // namespace

    void PathIntegrator::Start(const Scene &scene)
    {
        m_scene = &scene;
        // Build light sampling tables once per group.
        m_ls.clear();
        for (int g = 0; g < scene.LightGroupCount(); ++g)
            m_ls.push_back(CreateLightsSamplingStrategy(m_lightStrategy,
                                                        scene.GroupLights(g), g, scene));
    }

    namespace
    {
        // Tile context for a compacted walk.
        struct WalkCtx
        {
            const Scene *scene;
            const EmbreeScene *embree;
            const BsdfPtrTable *table;
            int nLights;
            // Light sampling tables.
            const LightsSamplingStrategy *ls = nullptr;
            // Light pointers and infinite flags indexed by light id.
            std::vector<const Light *> lightPtrs;
            std::vector<float> lightInf;
            // Area light pointers indexed by light ID.
            std::vector<const Light *> areaPtrs;
            // Group index of each area light.
            std::vector<std::int32_t> areaGroup;
            int activeGroup = 0;
            bool multiGroup = false;
            bool FULL_MIS, NEE, includeEnv, rrEfficiency;
            int maxDepth;
            FloatP rrProb, INF;
            // Media.
            const MediumPtrTable *mediumTable = nullptr;
            bool hasMedia = false;
        };

        // Addressing for stream keying.
        struct StreamAddr
        {
            UInt32P seed, sampleIdx;
            Int32P bounce, depth;
        };

        // Closest-hit shadow transmittance from origin along dir toward a
        // light at distance tMax.
        SWCSpectrumP ShadowTransmittance(const WalkCtx &ctx,
                                         const Point3fP &origin,
                                         const Vector3fP &dir,
                                         const UInt32P &startMedium,
                                         const FloatP &tMax,
                                         const SpectrumWavelengthsP &sw,
                                         const StreamAddr &addr,
                                         const UInt32P &lightIdx,
                                         MaskP active)
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
            UInt32P curMedium = startMedium;
            FloatP tCur = FloatP(EPS_RAY);
            MaskP tracing = active;
            // Shadow stream.
            RNGP rng = KeyedRNG(addr.seed, addr.sampleIdx, addr.bounce,
                                addr.depth, lightIdx, SHADOW_SALT);

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

                // Transmittance over the interval just travelled.
                const MediumPtr med =
                    ctx.mediumTable->Gather(curMedium, tracing);
                const FloatP segEnd = select(didHit, hit.t, tMax);
                T = select(tracing,
                           T * med->Transmittance(sw, shadow, tCur, segEnd, rng,
                                                  tracing),
                           T);

                // Missed everything but reached the light, this lane is done.
                tracing = tracing && !missed;
                if (none(tracing))
                    break;

                // Classify the surface such that null boundaries pass through,
                // opaque surfaces occlude.
                const BSDFPtr bsdf = ctx.table->Gather(hit.matID, tracing);
                const UInt32P bflags = bsdf->flags();
                const MaskP isNull = has_flag(bflags, BSDFType::Null);

                const MaskP opaque = tracing && !isNull;
                T = select(opaque, SWCSpectrumP(0.f), T);
                tracing = tracing && !opaque;

                // Null boundary, swap medium by the outgoing direction
                // and continue past the surface.
                if (any(tracing))
                {
                    curMedium = SwapMedium(hit, dir);
                    tCur = select(tracing, hit.t + FloatP(EPS_RAY), tCur);
                }
            }
            // Lanes still tracing after the crossing cap are treated as
            // occluded.
            T = select(tracing, SWCSpectrumP(0.f), T);
            return T;
        }

        // Persistent path state per SIMD lane.
        struct PathLanes
        {
            RayP ray;
            SpectrumWavelengthsP sw;
            SWCSpectrumP L, throughput;
            FloatP alpha, prevPdf;
            Point3fP prevP;
            Normal3fP prevN;
            Int32P depth;     // semantic bounce count frozen on Null
            Int32P bounce;    // RNG stream counter advances every bounce
            Int32P crossings; // null crossings within the current segment
            MaskP alive, specularBounce;
            // The previous vertex was a medium scatter.
            MaskP prevIsMedium;
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
            const int nLights = ctx.nLights;
            // Regular light dims d0+3..5 are unused with `All`.
            const bool neeAll =
                ctx.ls->kind == LightsSamplingStrategy::StrategyKind::All;
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
            MaskP &prevIsMedium = st.prevIsMedium;
            Int32P &bounce = st.bounce;
            Int32P &crossings = st.crossings;
            const SpectrumWavelengthsP &sw = st.sw;

            // Lane stream base. The block is 6 dims for geometry
            // and 7 dims for media.
            const bool hasMedia = ctx.hasMedia;
            const UInt32P stride = hasMedia ? UInt32P(7u) : UInt32P(6u);
            const UInt32P d0 =
                UInt32P(2u) + UInt32P(bounce) * stride;
            const Point2fP u2bsdf = src.Get2D(d0);
            const FloatP ucomp = src.Get1D(d0 + UInt32P(1u));
            const FloatP urr = src.Get1D(d0 + UInt32P(2u));
            // In `All` dims d0+3..5 are unused so skip hash and radical inverse.
            const FloatP ulight = neeAll ? FloatP(0.f) : src.Get1D(d0 + UInt32P(3u));
            const Point2fP u2lpos = neeAll ? Point2fP(0.f) : src.Get2D(d0 + UInt32P(4u));
            const FloatP ulightcomp = neeAll ? FloatP(0.f) : src.Get1D(d0 + UInt32P(5u));
            // Flight distance sample (media only).
            const FloatP uFlight =
                hasMedia ? src.Get1D(d0 + UInt32P(6u)) : FloatP(0.f);

            ray.alive = alive;
            HitP hit;
            embree->Intersect(ray, hit, Coherent::No);

            // -----------------------------------------------------------------
            // Medium flight
            // -----------------------------------------------------------------

            MaskP mediumEvent(false);
            MediumPtr medVert(nullptr);
            Point3fP medP(0.f);
            FloatP medK(0.f);
            if (hasMedia)
            {
                // Gathered non-vacuum.
                const MaskP inMedium =
                    alive && ctx.mediumTable->GatherNonVacuum(ray.medium, alive);
                if (any(inMedium))
                {
                    // Segment end is the nearest surface hit, else INF.
                    const FloatP tEnd =
                        select(hit.hit, hit.t, INF);

                    MediumSampleP ms;
                    ClearMediumSample(&ms, tEnd);
                    const MediumPtr med =
                        ctx.mediumTable->Gather(ray.medium, inMedium);
                    // Flight stream.
                    RNGP rng = KeyedRNG(st.seed, st.sampleIdx, bounce, depth,
                                        UInt32P(0u), FLIGHT_SALT);
                    med->SampleInteraction(sw, ray, ray.mint, tEnd, uFlight,
                                           rng, &ms, inMedium);

                    // Add gathered emission, then scale throughput by the
                    // flight weight (L += T*emission; T *= weight).
                    L = select(inMedium, L + throughput * ms.emission, L);
                    throughput = select(inMedium, throughput * ms.weight, throughput);

                    // Absorbed or exhausted lanes retire before the
                    // surface branch.
                    alive &= !(inMedium && ms.absorbed);

                    mediumEvent = inMedium && ms.scattered;
                    // Position evaluated inside the walk.
                    medP = ms.p;
                    medK = ms.k;
                    // wo for a medium vertex is -ray.d.
                    medVert = med;

                    // A medium escape that also missed all geometry retires
                    // via the miss branch.
                }
            }

            // Environment / infinite light.
            const MaskP miss = alive && !hit.hit && !mediumEvent;
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
                    // Iterate the lightgroup filtered light list.
                    for (int k = 0; k < nLights; ++k)
                    {
                        const Light *light = ctx.lightPtrs[k];
                        if (!light->IsInfinite())
                            continue;
                        const MaskP la = envCount;
                        if (none(la))
                            continue;
                        FloatP envW(1.f);
                        if (FULL_MIS && NEE)
                        {
                            // Skip the receiver hemisphere test for a
                            // medium previous vertex.
                            const MaskP recvHasN = !prevIsMedium;
                            const FloatP pdfLdir =
                                light->Pdf_L(prevP, prevN, Point3fP(0.f),
                                             Normal3fP(0.f), la, recvHasN) *
                                FloatP(ctx.ls->pSel[k]);
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

            // Hit! Lanes scattered inside a medium this bounce are not
            // shaded here.
            const MaskP hitMask = alive && hit.hit && !mediumEvent;
            // Enoki arrays are uninitialized on default construction.
            DifferentialGeometryP dg;
            dg.n = Normal3fP(0.f);
            dg.ng = Normal3fP(0.f);
            BSDFPtr bsdf(nullptr);
            Normal3fP ng(FloatP(0.f));
            if (any(hitMask))
            {
                // A primary ray that lands on geometry is opaque coverage.
                alpha = select(hitMask && eq(depth, Int32P(0)), FloatP(1.f), alpha);

                const Point3fP p = hit.p;
                ng = hit.ngeo;
                const MaskP entering = dot(ray.d, ng) < FloatP(0.f);
                // lux never flips the shading normal to face the ray, BxDFs
                // work in a frame built on dgShading.nn and side logic runs
                // through ng.
                const Normal3fP n = hit.sh_n;

                dg.p = p;
                // Zero the shading/geometric normals off the surface lanes.
                dg.n = select(hitMask, n, Normal3fP(0.f));
                dg.ng = select(hitMask, ng, Normal3fP(0.f));
                dg.entering = entering;
                dg.uv_u = hit.uv.x();
                dg.uv_v = hit.uv.y();
                // Shading frame from the ShadeHit UV gradient solve.
                dg.dp_du = hit.dp_du;
                dg.dp_dv = hit.dp_dv;
                dg.dp_ds = hit.dp_ds;
                dg.dp_dt = hit.dp_dt;

                bsdf = table.Gather(hit.matID, hitMask);
                const Vector3fP wo = -ray.d;

                // Emission on hit.
                MaskP emCount = hitMask && (hit.lightID >= Int32P(0));
                if (!FULL_MIS && NEE)
                    emCount = emCount && specularBounce;
                // Only area lights belonging to the active lightgroup emit.
                if (ctx.multiGroup && any(emCount))
                {
                    const Int32P g = enoki::gather<Int32P>(
                        ctx.areaGroup.data(), hit.lightID, emCount);
                    emCount = emCount && eq(g, Int32P(ctx.activeGroup));
                }
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
                        // A medium previous vertex has prevN == 0, so the
                        // receiver hemisphere test must be skipped or Pdf_L
                        // returns 0 and misWeight overcounts.
                        const MaskP recvHasN = !prevIsMedium;
                        // Light sampling weight by area light id.
                        const FloatP pSel =
                            enoki::gather<FloatP>(ctx.ls->areaPSel.data(),
                                                  hit.lightID, la);
                        const FloatP pdfLEmitter =
                            al->Pdf_L(prevP, prevN, p, n, la, recvHasN) * pSel;
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
            }

            // -----------------------------------------------------------------
            // Vertex handling
            // -----------------------------------------------------------------
            const MaskP valid = hitMask | mediumEvent;
            if (any(valid))
            {
                VertexP vtx;
                vtx.valid = valid;
                vtx.isMedium = mediumEvent;
                vtx.p = select(mediumEvent, medP, hit.p);
                // dg is only filled on surface lanes.
                // Medium lanes carry n == ng == 0.
                vtx.dg = dg;
                // wo points back along the incoming segment.
                vtx.wo = -ray.d;
                vtx.k = medK;
                vtx.bsdf = bsdf; // null on medium lanes
                // medVert is non-null for any lane that was inside a medium.
                vtx.med = medVert;

                // Depth limit.
                alive &= !(valid && depth >= Int32P(maxDepth));
                const MaskP vtxAlive = valid && alive;

                // NEE.
                const MaskP neeGate = vtxAlive;
                if (NEE && nLights > 0 && any(neeGate))
                {
                    // Shadow origin where surface vertices offset toward the
                    // arrival side and medium vertices start at the event.
                    const Point3fP surfOff =
                        OffsetRay(hit.p, ng, vtx.wo, FloatP(EPS_RAY));
                    const Point3fP shadowO =
                        select(mediumEvent, medP, surfOff);

                    // One NEE sample weighted by pSel.
                    auto neeOne = [&](const auto &light, const UInt32P &shadowKey,
                                      const FloatP &infF, const FloatP &pSel,
                                      const Point2fP &u2, const FloatP &uc,
                                      const MaskP &la) -> SWCSpectrumP
                    {
                        Point3fP lightP(0.f, 0.f, 0.f);
                        Vector3fP wiL(0.f, 0.f, 0.f);
                        Normal3fP lightN(0.f, 0.f, 0.f);
                        FloatP pdfPos(0.f);
                        SWCSpectrumP Le(0.f);
                        // Medium receivers have no shading normal.
                        const MaskP recvHasN = !mediumEvent;
                        const MaskP sampleOk = light->Sample_Li(
                            sw, vtx.p, vtx.dg.n, u2.x(), u2.y(), uc,
                            &lightP, &wiL, &lightN, &pdfPos, &Le, la, recvHasN);
                        const FloatP dist = enoki::norm(lightP - vtx.p);
                        const FloatP pdfL = pdfPos * pSel;

                        // The shadow ray leaves through the face wiL crosses.
                        const UInt32P shadowMedium =
                            select(mediumEvent, ray.medium, SwapMedium(hit, wiL));

                        // Stop before a finite emitter with an absolute margin.
                        const FloatP tmax =
                            select(infF > FloatP(0.5f), INF,
                                   dist - enoki::max(FloatP(1e-3f),
                                                     FloatP(1e-3f) * dist));

                        // Skip shadow and SDF when no lane can contribute.
                        const MaskP tryVis = la && sampleOk && (pdfL > FloatP(0.f));
                        if (none(tryVis))
                            return SWCSpectrumP(0.f);

                        const StreamAddr addr{st.seed, st.sampleIdx, bounce, depth};
                        const SWCSpectrumP Tr = ShadowTransmittance(
                            ctx, shadowO, wiL, shadowMedium, tmax, sw, addr,
                            shadowKey, tryVis);

                        // SDF Eval on surface lanes, phase pdf on medium lanes.
                        BSDFEvalP ev;
                        FloatP cosTerm(FloatP(0.f));
                        EvalVertex(vtx, sw, wiL, &ev, &cosTerm, la);

                        FloatP neeW(1.f);
                        if (FULL_MIS)
                            neeW = misWeight(pdfL, ev.pdf);
                        // Medium lanes, ev.f == phaseVal.
                        const MaskP ok =
                            tryVis && (Tr > SWCSpectrumP(0.f)) &&
                            (pdfL > FloatP(0.f)) && (cosTerm > FloatP(0.f));
                        return select(
                            ok,
                            neeW * throughput * Le * ev.f * Tr /
                                max(pdfL, FloatP(EPS_DENOM)),
                            SWCSpectrumP(0.f));
                    };

                    if (ctx.ls->kind == LightsSamplingStrategy::StrategyKind::All)
                    {
                        // One task per slot.
                        const uint32_t nT = ctx.ls->TaskCount();
                        for (uint32_t s = 0; s < nT; ++s)
                        {
                            const uint32_t k = ctx.ls->task[s];
                            // Two dims per slot.
                            const UInt32P d =
                                UInt32P(NEE_ALL_DIM_BASE) +
                                (UInt32P(bounce) * UInt32P(nT) +
                                 UInt32P(s)) *
                                    UInt32P(2u);
                            L = L + neeOne(ctx.lightPtrs[k], UInt32P(s),
                                           FloatP(ctx.lightInf[k]),
                                           FloatP(ctx.ls->pSel[k]),
                                           src.Get2D(d), src.Get1D(d + UInt32P(1u)),
                                           neeGate);
                        }
                    }
                    else
                    {
                        const LightPick pick =
                            SampleLight(*ctx.ls, ulight, neeGate);
                        const UInt32P idx = pick.idx;
                        const FloatP pSel = pick.pSel;
                        const LightPtr light =
                            enoki::gather<LightPtr>(ctx.lightPtrs.data(), idx, neeGate);
                        const FloatP infF =
                            enoki::gather<FloatP>(ctx.lightInf.data(), idx, neeGate);
                        L = L + neeOne(light, idx, infF, pSel, u2lpos,
                                       ulightcomp, neeGate);
                    }
                }

                // Continuation direction sample.
                Point2fP uPhase = u2bsdf;
                if (hasMedia && any(mediumEvent))
                    uPhase = src.Get2D(d0 + UInt32P(1u));
                const FloatP su1 = select(mediumEvent, uPhase.x(), u2bsdf.x());
                const FloatP su2 = select(mediumEvent, uPhase.y(), u2bsdf.y());

                BSDFSampleP sample;
                SampleVertex(vtx, sw, su1, su2, ucomp, &sample, vtxAlive);

                // Zero pdf continuation kills the path. Medium lanes are
                // covered too.
                alive &= !(vtxAlive && (sample.pdf <= FloatP(0.f)));

                // Russian roulette per vertex. Surface lanes use the
                // lobe value sample.f; medium lanes use throughput
                // because sample.f is 1 by construction.
                const MaskP rrActive = alive && (depth > Int32P(RR_START));
                if (any(rrActive))
                {
                    const SWCSpectrumP tputF =
                        select(mediumEvent, throughput, sample.f);
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

                // A medium vertex is not specular when NEE is on, a surface
                // vertex takes the sampled lobe's flag.
                const MaskP neeOn = MaskP(NEE);
                specularBounce =
                    select(valid,
                           select(mediumEvent, specularBounce && !neeOn,
                                  has_flag(sample.sampledType,
                                           BSDFType::Specular)),
                           specularBounce);

                // Surface vertices offset along ng, medium vertices step
                // a fixed epsilon.
                const Point3fP surfSpawn =
                    OffsetRay(hit.p, ng, sample.wi, FloatP(EPS_RAY));
                const Point3fP medSpawn = medP + sample.wi * FloatP(EPS_RAY);
                const Point3fP spawn =
                    select(mediumEvent, medSpawn, surfSpawn);

                // Medium swap where surface vertices cross the boundary the sampled
                // direction leaves through.
                const UInt32P newMedium = SwapMedium(hit, sample.wi);

                const RayP contRay(spawn, sample.wi, FloatP(EPS_RAY), INF,
                                   FloatP(0.f));
                ray.o = select(valid, contRay.o, ray.o);
                ray.d = select(valid, contRay.d, ray.d);
                ray.d_rcp = select(valid, contRay.d_rcp, ray.d_rcp);
                ray.mint = select(valid, contRay.mint, ray.mint);
                ray.maxt = select(valid, contRay.maxt, ray.maxt);
                ray.time = select(valid, contRay.time, ray.time);
                ray.wavelengths = select(valid, sw.w, ray.wavelengths);
                ray.mask = select(valid, UInt32P(0xFFFFFFFFu), ray.mask);
                ray.medium = select(mediumEvent, ray.medium,
                                    select(hitMask, newMedium, ray.medium));

                // Bookkeeping.
                const MaskP isNull =
                    has_flag(sample.sampledType, BSDFType::Null);
                bounce = select(valid, bounce + Int32P(1), bounce);
                depth = select(valid && !isNull, depth + Int32P(1), depth);
                crossings = select(valid,
                                   select(isNull, crossings + Int32P(1),
                                          Int32P(0)),
                                   crossings);
                alive &= !(vtxAlive &&
                           (crossings > Int32P(MAX_NULL_CROSSINGS)));
                prevP = select(valid, vtx.p, prevP);
                prevN = select(valid, vtx.dg.n, prevN);
                prevPdf = select(valid, sample.pdf, prevPdf);
                prevIsMedium = select(valid, mediumEvent, prevIsMedium);
            }
        }

        // Pixel sample handed to each lane.
        struct Job
        {
            int px, py;
            uint32_t s;
        };

        // Enumerate (px, py, s) jobs over tiles in scanline order.
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

        // Fill dead lanes with next jobs.
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

            // Fold the sampler's base seed.
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
                const uint64_t h = PixelSeed(sampleBasis, sampler.BaseSeed(), j.px, j.py);
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
            // Primary rays start in clear space. TODO: camera inside volume.
            st.ray.medium =
                select(fillMask, UInt32P(LUX2_MEDIUM_CLEAR), st.ray.medium);

            st.sw.w = select(fillMask, newSw.w, st.sw.w);
            st.sw.binRGB = select(fillMask, newSw.binRGB, st.sw.binRGB);
            st.sw.offsetRGB = select(fillMask, newSw.offsetRGB, st.sw.offsetRGB);
            st.sw.binXYZ = select(fillMask, newSw.binXYZ, st.sw.binXYZ);
            st.sw.offsetXYZ = select(fillMask, newSw.offsetXYZ, st.sw.offsetXYZ);

            st.L = select(fillMask, SWCSpectrumP(0.f), st.L);
            st.throughput = select(fillMask, SWCSpectrumP(1.f), st.throughput);
            st.alpha = select(fillMask, FloatP(0.f), st.alpha);
            st.depth = select(fillMask, Int32P(0), st.depth);
            st.bounce = select(fillMask, Int32P(0), st.bounce);
            st.crossings = select(fillMask, Int32P(0), st.crossings);
            // First bounce is treated as specular for MIS weighting.
            st.specularBounce = select(fillMask, MaskP(true), st.specularBounce);
            st.prevP = select(fillMask, cam.o, st.prevP);
            st.prevN = select(fillMask, Normal3fP(0.f, 0.f, 0.f), st.prevN);
            st.prevPdf = select(fillMask, FloatP(0.f), st.prevPdf);
            // A primary ray has the camera as its previous vertex.
            st.prevIsMedium = select(fillMask, MaskP(false), st.prevIsMedium);
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
        OccupancyBegin();

        // Tile constant config.
        WalkCtx ctx;
        ctx.scene = &scene;
        ctx.embree = scene.GetEmbree();
        ctx.table = &scene.GetBsdfTable();
        ctx.mediumTable = &scene.GetMediumTable();
        ctx.hasMedia = scene.HasMedia();

        // Active lightgroup filtering.
        const int activeGroup = scene.GetActiveGroup();
        ctx.activeGroup = activeGroup;
        ctx.multiGroup = scene.LightGroupCount() > 1;
        ctx.lightPtrs = scene.GroupLights(activeGroup);
        ctx.lightInf = scene.GroupLightInf(activeGroup);
        ctx.nLights = static_cast<int>(ctx.lightPtrs.size());
        // Sampling tables indexed by active light group.
        LightsSamplingStrategy fallback;
        if (activeGroup >= 0 &&
            activeGroup < static_cast<int>(m_ls.size()))
            ctx.ls = &m_ls[activeGroup];
        else
        {
            // Fallback for a caller that runs RenderTile without Start()
            // (e.g. a scene with no light groups).
            fallback = CreateLightsSamplingStrategy(m_lightStrategy, ctx.lightPtrs,
                                                    activeGroup, scene);
            ctx.ls = &fallback;
        }

        const int nArea = scene.AreaLightCount();
        ctx.areaPtrs.reserve(nArea);
        for (int a = 0; a < nArea; ++a)
            ctx.areaPtrs.push_back(scene.GetAreaLight(a));
        if (ctx.multiGroup)
        {
            ctx.areaGroup.resize(nArea);
            for (int a = 0; a < nArea; ++a)
                ctx.areaGroup[a] = scene.AreaLightGroup(a);
        }
        ctx.FULL_MIS = (m_lightMode == LightMode::MIS);
        ctx.NEE = (m_lightMode != LightMode::BSDF) && m_directLightSampling;
        ctx.includeEnv = m_includeEnvironment;
        ctx.maxDepth = m_maxDepth;
        ctx.rrEfficiency = (m_rrStrategy == "efficiency");
        ctx.rrProb = FloatP(m_rrContinueProb);
        ctx.INF = FloatP(INF_F);

        // All lanes start dead.
        PathLanes st;
        st.alive = MaskP(false);
        st.prevIsMedium = MaskP(false);
        JobCursor jobs(tile, spp);
        RetireBuffer retire;

        Refill(camera, sampler, jobs, sampleBasis, st, 1);
        while (any(st.alive))
        {
            // Active lanes this bounce.
            OccupancyStep(enoki::count(st.alive));
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
        OccupancyTileEnd("path", tile.x0, tile.y0, unsigned(PACKET_WIDTH));
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
        // Light sampling strategy.
        const std::string lightStrategy =
            p ? p->FindOneString("lightstrategy", "one") : "one";

        return std::make_shared<PathIntegrator>(
            maxDepth, rrContinueProb, rrStrategy,
            includeEnvironment, directLightSampling, lightStrategy, lightMode);
    }

    LUX2_REGISTER_SURFACE_INTEGRATOR(PathIntegrator, "path");

} // namespace lux2
