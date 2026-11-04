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

// Path as a consumer of the shared walker. The transport step lives in
// Advance(); everything here is path's estimator: MIS-weighted emission and
// environment escape (OnEvent), next-event estimation at a vertex (OnVertex),
// Russian roulette (Survival), and the sampler dimension layout (Dims).
//
// A Null surface is collapsed inside Advance, so it never reaches OnVertex --
// no bounce, no NEE, no RNG block. Its emission is accumulated in
// OnPassThrough instead.

#ifndef LUX2_INTEGRATORS_CONSUMERS_PATH_CONSUMER_H
#define LUX2_INTEGRATORS_CONSUMERS_PATH_CONSUMER_H

#include "integrators/core/vertex.h"
#include "integrators/lane/advance.h"
#include "integrators/lane/direct.h"
#include "integrators/lane/geom.h"
#include "integrators/lane/jobs.h"
#include "integrators/lane/keys.h"
#include "integrators/lane/lane_core.h"
#include "integrators/lane/math.h"
#include "integrators/lane/sampler_source.h"
#include "integrators/lane/tile_ctx.h"

#include "core/light.h"
#include "core/math/math.h"
#include "core/phase.h"
#include "core/sampler.h"

#include <enoki/array.h>
#ifdef LUX2_COLLAPSE_DEBUG
#include <iostream>
#endif

namespace lux2
{
    // Estimator switches, set once per tile from the integrator's parameters.
    struct PathConfig
    {
        bool FULL_MIS = false, NEE = false, includeEnv = false;
        bool rrEfficiency = false;
        int maxDepth = 8;
        FloatP rrProb = FloatP(0.95f);
    };

    // Persistent path state per SIMD lane: the shared core plus path's tail.
    struct PathLanes
    {
        LaneCore core;
        SWCSpectrumP L;
        FloatP alpha, prevPdf;
        Point3fP prevP;
        Normal3fP prevN;
        Int32P depth;  // semantic bounce count (real vertices only)
        Int32P bounce; // RNG stream counter, advances every real vertex
        MaskP specularBounce;
        MaskP prevIsMedium;
        MaskP prevRecvHasN;
        FloatP x, y, camWeight; // film position and weight for retire

        static PathLanes Fresh(const PixelPlan &plan)
        {
            const RayP &cam = plan.primary;
            PathLanes s;
            s.core.o = cam.o;
            s.core.d = cam.d;
            s.core.mint = cam.mint;
            s.core.maxt = cam.maxt;
            s.core.time = cam.time;
            s.core.medium = UInt32P(LUX2_MEDIUM_CLEAR);
            s.core.sw = plan.sw;
            s.core.beta = SWCSpectrumP(1.f);
            s.core.seed = plan.seed;
            s.core.sampleIdx = plan.sampleIdx;
            s.core.alive = plan.fill;

            s.L = SWCSpectrumP(0.f);
            s.alpha = FloatP(0.f);
            s.prevPdf = FloatP(0.f);
            // A primary ray has the camera as its previous vertex.
            s.prevP = cam.o;
            s.prevN = Normal3fP(0.f, 0.f, 0.f);
            s.depth = Int32P(0);
            s.bounce = Int32P(0);
            // First bounce is treated as specular for MIS weighting.
            s.specularBounce = MaskP(true);
            s.prevIsMedium = MaskP(false);
            s.prevRecvHasN = MaskP(true);
            s.x = plan.x;
            s.y = plan.y;
            s.camWeight = plan.camWeight;
            return s;
        }
    };

    LUX2_BLEND(PathLanes, core, L, alpha, prevPdf, prevP, prevN, depth, bounce,
               specularBounce, prevIsMedium, prevRecvHasN, x, y, camWeight)

    class PathConsumer
    {
      public:
        using Lanes = PathLanes;

        PathConsumer(const TileCtx &c, const PathConfig &cfg)
            : m_c(c), m_cfg(cfg) {}

        // --- Advance's accessors: counters live in the lane tail. -----------
        Int32P Bounce(const LaneCore &core) const { return mL.bounce; }
        Int32P DepthBase(const LaneCore &core) const { return mL.depth; }

        // An emissive Null crossing: accumulate its radiance (s = 0) at the
        // current throughput. No vertex, no bounce, no NEE -- the ray continues.
        void OnPassThrough(LaneCore &core, const HitP &h, const MaskP &m)
        {
            if (!any(m))
                return;
#ifdef LUX2_COLLAPSE_DEBUG
            {
                int32_t lid[PACKET_WIDTH];
                uint32_t mm[PACKET_WIDTH];
                enoki::store_unaligned(lid, h.lightID);
                enoki::store_unaligned(mm, m);
                for (size_t i = 0; i < PACKET_WIDTH; ++i)
                    if (mm[i])
                        std::cerr << "[passthru] lightID=" << lid[i]
                                  << " nLights=" << m_c.nLights << "\n";
            }
#endif
            const SpectrumWavelengthsP sw = core.sw;
            const Vector3fP wo = -core.d;
            const DifferentialGeometryP dg = MakeDG(h, core.d, m);
            MaskP emCount = m && (h.lightID >= Int32P(0));
            if (m_c.multiGroup && any(emCount))
            {
                const Int32P g = enoki::gather<Int32P>(
                    m_c.areaGroup.data(), h.lightID, emCount);
                emCount = emCount && eq(g, Int32P(m_c.activeGroup));
            }
            if (!any(emCount))
                return;
            const LightPtr al = enoki::gather<LightPtr>(
                m_c.areaPtrs.data(), h.lightID, emCount);
            const SWCSpectrumP LeRaw = al->Le(dg, sw, wo, emCount);
            mL.L = select(emCount, mL.L + core.beta * LeRaw, mL.L);
        }

        // Emission at a real vertex and the environment escape, both with the
        // MIS weight against the previous vertex's NEE strategy.
        void OnEvent(LaneCore &core, const Event &ev)
        {
            m_mediumEvent = ev.scatter;
            SWCSpectrumP &L = mL.L;
            SWCSpectrumP &throughput = core.beta;
            FloatP &alpha = mL.alpha;
            const Int32P &depth = mL.depth;
            MaskP &alive = core.alive;
            const SpectrumWavelengthsP &sw = core.sw;
            const Vector3fP rayD = core.d;

            // Medium flight radiance, gathered at preflight beta.
            L = L + ev.flightEmission;

            const MaskP hitMask = ev.surface;
            const HitP &hit = ev.hit;
            const DifferentialGeometryP &dg = ev.dg;
            const Vector3fP wo = -rayD;

            // Environment / infinite light on escape.
            const MaskP miss = alive && ev.escape;
            if (any(miss))
            {
                const MaskP envOn = MaskP(m_cfg.includeEnv);
                const MaskP deepper = depth > Int32P(0);
                MaskP envCount;
                if (m_cfg.FULL_MIS)
                    envCount = miss && (envOn || deepper);
                else
                    envCount = miss &&
                               (MaskP(!m_cfg.NEE) ||
                                ((envOn || deepper) && mL.specularBounce));
                if (m_c.nLights > 0 && any(envCount))
                {
                    RayP ray = MakeRay(core);
                    for (int k = 0; k < m_c.nLights; ++k)
                    {
                        const Light *light = m_c.lightPtrs[k];
                        if (!light->IsInfinite())
                            continue;
                        const MaskP la = envCount;
                        if (none(la))
                            continue;
                        FloatP envW(1.f);
                        if (m_cfg.FULL_MIS && m_cfg.NEE)
                        {
                            const FloatP pdfLdir =
                                light->Pdf_L(mL.prevP, mL.prevN, Point3fP(0.f),
                                             Normal3fP(0.f), la,
                                             mL.prevRecvHasN) *
                                FloatP(m_c.lsD->pSel[k]);
                            envW = select(mL.specularBounce || eq(depth, Int32P(0)),
                                          FloatP(1.f),
                                          misWeight(mL.prevPdf, pdfLdir));
                        }
                        L = select(la, L + envW * throughput * light->Le(ray, la),
                                   L);
                    }
                }
                alpha = select(miss && eq(depth, Int32P(0)), FloatP(0.f), alpha);
                alive &= !miss;
            }

            // Emission on a real surface hit.
            if (any(hitMask))
            {
                alpha = select(hitMask && eq(depth, Int32P(0)), FloatP(1.f),
                               alpha);
                MaskP emCount = hitMask && (hit.lightID >= Int32P(0));
                if (!m_cfg.FULL_MIS && m_cfg.NEE)
                    emCount = emCount && mL.specularBounce;
                if (m_c.multiGroup && any(emCount))
                {
                    const Int32P g = enoki::gather<Int32P>(
                        m_c.areaGroup.data(), hit.lightID, emCount);
                    emCount = emCount && eq(g, Int32P(m_c.activeGroup));
                }
                if (any(emCount))
                {
                    const MaskP la = emCount;
                    const LightPtr al = enoki::gather<LightPtr>(
                        m_c.areaPtrs.data(), hit.lightID, la);
                    FloatP emW(1.f);
                    if (m_cfg.FULL_MIS && m_cfg.NEE)
                    {
                        const FloatP pSel = enoki::gather<FloatP>(
                            m_c.lsD->areaPSel.data(), hit.lightID, la);
                        const FloatP pdfLEmitter =
                            al->Pdf_L(mL.prevP, mL.prevN, dg.p, dg.n, la,
                                      mL.prevRecvHasN) *
                            pSel;
                        emW = select(mL.specularBounce, FloatP(1.f),
                                     misWeight(mL.prevPdf, pdfLEmitter));
                    }
                    const SWCSpectrumP LeRaw = al->Le(dg, sw, wo, la);
                    L = select(la, L + emW * throughput * LeRaw, L);
                }
            }
        }

        // NEE at a real vertex (power heuristic), then sample the continuation
        // direction. Survival and PostVertex finish the bounce.
        void OnVertex(const SamplerSource &src, LaneCore &core, const Event &ev)
        {
            SWCSpectrumP &L = mL.L;
            SWCSpectrumP &throughput = core.beta;
            MaskP &alive = core.alive;
            Int32P &depth = mL.depth;
            const Int32P &bounce = mL.bounce;
            const SpectrumWavelengthsP &sw = core.sw;

            const MaskP mediumEvent = m_mediumEvent;
            const MaskP hitMask = ev.surface;
            const HitP &hit = ev.hit;
            const Vector3fP wo = -core.d;

            const MaskP valid = hitMask | mediumEvent;
            if (!any(valid))
                return;

            VertexP vtx;
            vtx.valid = valid;
            vtx.isMedium = mediumEvent;
            vtx.p = select(mediumEvent, ev.hit.p, hit.p);
            vtx.dg = ev.dg; // zeroed on medium lanes
            vtx.wo = wo;
            vtx.k = ev.k;
            vtx.bsdf = ev.bsdf; // null on medium lanes
            vtx.med = ev.med;

            // Depth limit.
            alive &= !(valid && depth >= Int32P(m_cfg.maxDepth));
            const MaskP vtxAlive = valid && alive;

            // NEE.
            if (m_cfg.NEE && m_c.nLights > 0 && any(vtxAlive))
            {
                const uint32_t nT = m_c.lsD->TaskCount();
                const DimsT dims = Dims();
                ForEachNeeLight(
                    m_c, src, vtxAlive,
                    [&](uint32_t s) {
                        return NEE_ALL_DIM +
                               (UInt32P(bounce) * UInt32P(nT) + UInt32P(s)) *
                                   UInt32P(2u);
                    },
                    NeeOneDims{dims.sel, dims.pos, dims.comp},
                    [&](const auto &light, const UInt32P &,
                        const UInt32P &shadowKey, const FloatP &infF,
                        const FloatP &pSel, const Point2fP &u2,
                        const FloatP &uc, const MaskP &la) {
                        const LiSample li = SampleLi(
                            sw, light, infF, vtx.p, vtx.dg.n,
                            RecvHasNormal(hitMask, ev.bsdf), pSel, u2, uc, la);
                        const ShadowRng rng{core.seed, core.sampleIdx, bounce,
                                            depth, shadowKey, SHADOW_SALT};
                        const DirectEval de = ShadeDirect(
                            m_c.shadow, vtx, sw, li, rng,
                            [&](const Vector3fP &wiL) {
                                return select(mediumEvent, core.medium,
                                              SwapMedium(hit, wiL));
                            });
                        if (!any(de.ok))
                            return;
                        FloatP neeW(1.f);
                        if (m_cfg.FULL_MIS)
                            neeW = misWeight(li.pdfL, de.ev.pdf);
                        L = L + select(
                                  de.ok,
                                  neeW * throughput * li.Le * de.ev.f * de.Tr /
                                      max(li.pdfL, FloatP(EPS_DENOM)),
                                  SWCSpectrumP(0.f));
                    });
            }

            // Continuation direction sample.
            const DimsT dims = Dims();
            Point2fP u2bsdf = src.Get2D(dims.base);
            const FloatP ucomp = src.Get1D(dims.base + UInt32P(1u));
            m_urr = src.Get1D(dims.base + UInt32P(2u));
            if (m_c.hasMedia && any(mediumEvent))
                u2bsdf = src.Get2D(dims.base + UInt32P(1u));
            const FloatP su1 = select(mediumEvent, u2bsdf.x(),
                                      src.Get2D(dims.base).x());
            const FloatP su2 = select(mediumEvent, u2bsdf.y(),
                                      src.Get2D(dims.base).y());

            SampleVertex(vtx, sw, su1, su2, ucomp, &m_sample, vtxAlive);
            // Zero pdf continuation kills the path.
            alive &= !(vtxAlive && (m_sample.pdf <= FloatP(0.f)));
        }

        // Russian roulette per vertex. Surface lanes use the lobe value
        // sample.f; medium lanes use throughput (sample.f is 1 by construction).
        void Survival(LaneCore &core)
        {
            const MaskP rrActive = core.alive && (mL.depth > Int32P(RR_START));
            if (!any(rrActive))
                return;
            const SWCSpectrumP tputF =
                select(m_mediumEvent, core.beta, m_sample.f);
            FloatP q;
            if (m_cfg.rrEfficiency)
                q = min(FloatP(1.f), max(tputF, FloatP(0.f)));
            else
                q = m_cfg.rrProb;
            const MaskP kill = rrActive && (q < m_urr);
            core.beta = select(rrActive && !kill,
                               core.beta / max(q, FloatP(EPS_DENOM)),
                               core.beta);
            core.alive &= !kill;
        }

        // Shared post-Survival bookkeeping: throughput, spawn ray, medium swap,
        // counters and the previous-vertex MIS state.
        void PostVertex(LaneCore &core, const Event &ev)
        {
            SWCSpectrumP &throughput = core.beta;
            MaskP &alive = core.alive;
            Int32P &depth = mL.depth;
            Int32P &bounce = mL.bounce;

            const MaskP mediumEvent = m_mediumEvent;
            const MaskP hitMask = ev.surface;
            const HitP &hit = ev.hit;
            const MaskP valid = hitMask | mediumEvent;

            // throughput *= f on surviving lanes.
            throughput = select(alive, throughput * m_sample.f, throughput);

            // A medium vertex is not specular when NEE is on; a surface vertex
            // takes the sampled lobe's flag.
            const MaskP neeOn = MaskP(m_cfg.NEE);
            mL.specularBounce =
                select(valid,
                       select(mediumEvent, mL.specularBounce && !neeOn,
                              has_flag(m_sample.sampledType,
                                       BSDFType::Specular)),
                       mL.specularBounce);

            // Surface vertices offset along ng; medium vertices step epsilon.
            const Point3fP surfSpawn =
                OffsetRay(hit.p, hit.ngeo, m_sample.wi, FloatP(EPS_RAY));
            const Point3fP medSpawn = ev.hit.p + m_sample.wi * FloatP(EPS_RAY);
            const Point3fP spawn = select(mediumEvent, medSpawn, surfSpawn);
            const UInt32P newMedium = SwapMedium(hit, m_sample.wi);

            core.o = select(valid, spawn, core.o);
            core.d = select(valid, m_sample.wi, core.d);
            core.mint = select(valid, FloatP(EPS_RAY), core.mint);
            core.maxt = select(valid, FloatP(INF_F), core.maxt);
            core.time = select(valid, FloatP(0.f), core.time);
            core.medium = select(mediumEvent, core.medium,
                                 select(hitMask, newMedium, core.medium));

            // Counters. Null samples no longer occur here (Advance collapsed
            // them), so every valid vertex is a real one: bounce and depth both
            // advance, and the per-segment crossing counter is gone.
            bounce = select(valid, bounce + Int32P(1), bounce);
            depth = select(valid, depth + Int32P(1), depth);

            mL.prevP = select(valid, ev.hit.p, mL.prevP);
            mL.prevN = select(valid, ev.dg.n, mL.prevN);
            mL.prevPdf = select(valid, m_sample.pdf, mL.prevPdf);
            mL.prevIsMedium = select(valid, mediumEvent, mL.prevIsMedium);
            mL.prevRecvHasN =
                select(valid, RecvHasNormal(hitMask, ev.bsdf), mL.prevRecvHasN);
        }

        // Sampler dimensions for this bounce: a stride-6 (geometry) or stride-7
        // (media) block keyed by the bounce counter.
        struct DimsT
        {
            UInt32P base;
            UInt32P sel, pos, comp;
        };
        DimsT Dims() const
        {
            const UInt32P stride = m_c.hasMedia ? UInt32P(7u) : UInt32P(6u);
            const UInt32P base = UInt32P(2u) + UInt32P(mL.bounce) * stride;
            return DimsT{base, base + UInt32P(3u), base + UInt32P(4u),
                         base + UInt32P(5u)};
        }

        // The persistent lane tail, for the driver to read/blend on refill.
        PathLanes &Tail() { return mL; }
        void SetTail(const PathLanes &t) { mL = t; }

      private:
        const TileCtx &m_c;
        const PathConfig &m_cfg;
        PathLanes mL;
        // Per-bounce transport state, set by OnEvent and read downstream.
        MaskP m_mediumEvent = MaskP(false);
        BSDFSampleP m_sample;
        FloatP m_urr = FloatP(0.f);
    };

} // namespace lux2

#endif // LUX2_INTEGRATORS_CONSUMERS_PATH_CONSUMER_H
