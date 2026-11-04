/***************************************************************************
 *   Copyright (C) 1998-2026 by authors (see AUTHORS.txt)                  *
 *                                                                         *
 *   This file is free software; you can redistribute it and/or modify     *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   any later version.                                                    *
 *                                                                         *
 *   This project is based on PBRT; see <http://www.pbrt.org>              *
 ***************************************************************************/

// Wavefront store walker drivers.

#ifndef LUX2_INTEGRATORS_WF_WALKERS_H
#define LUX2_INTEGRATORS_WF_WALKERS_H

#include "integrators/lane/advance.h"
#include "integrators/lane/jobs.h"
#include "integrators/lane/keys.h"
#include "integrators/lane/light_pick.h"
#include "integrators/wf/scheduler.h"
#include "integrators/consumers/bidirectional_consumer.h"

#include "core/camera.h"
#include "core/renderinghints.h"
#include "core/sampler.h"

#include <algorithm>
#include <enoki/array.h>

namespace lux2
{
    namespace wf
    {
        // Allocate a pool path slot per filling lane.
        inline UInt32P AllocPaths(PathStore &ps, const MaskP &fill,
                                  std::vector<uint32_t> &takenOut)
        {
            alignas(64) uint32_t pathA[PACKET_WIDTH];
            alignas(64) float fillA[PACKET_WIDTH];
            enoki::store_unaligned(fillA, select(fill, FloatP(1.f), FloatP(0.f)));
            takenOut.clear();
            for (size_t i = 0; i < PACKET_WIDTH; ++i)
            {
                if (fillA[i] > 0.f && !ps.freePaths.empty())
                {
                    const uint32_t p = ps.freePaths.back();
                    ps.freePaths.pop_back();
                    // Reset the vertex count so stale n[p] is not read.
                    ps.n[p] = 0u;
                    pathA[i] = p;
                    takenOut.push_back(p);
                }
                else
                {
                    pathA[i] = 0u;
                }
            }
            return enoki::load_unaligned<UInt32P>(pathA);
        }

        // Write the camera vertex of a fresh eye path into the pool and
        // set the sample table row.
        inline void WriteCameraVertex(PathStore &ps, const WalkCtx &c,
                                      SampleTable &tbl, const EyeLanes &lane,
                                      const MaskP &fill)
        {
            const UInt32P rec0 = RecIdx(ps, lane.path, Int32P(0), fill);

            VertexGeomP g;
            g.p = lane.prevP; // camera origin
            g.n = Normal3fP(0.f);
            g.ng = Normal3fP(0.f);
            g.wb = Vector3fP(0.f);
            g.dp_du = Vector3fP(0.f);
            g.dp_dv = Vector3fP(0.f);
            g.dp_ds = Vector3fP(0.f);
            g.dp_dt = Vector3fP(0.f);
            g.uv = Point2fP(0.f);
            g.flux = SWCSpectrumP(1.f);
            g.k = FloatP(0.f);
            g.matID = UInt32P(0u);
            g.medID = c.cameraMediumID;
            g.medFront = c.cameraMediumID;
            g.medBack = c.cameraMediumID;
            StoreGeom(ps.arena->geomWords.data(), g, rec0, fill);

            VertexMisP mv;
            mv.dAW = FloatP(0.f);
            mv.dARW = FloatP(0.f);
            mv.rr = FloatP(1.f);
            mv.rrR = FloatP(1.f);
            mv.cosi = FloatP(0.f);
            mv.coso = FloatP(0.f);
            mv.d2 = FloatP(0.f);
            mv.bits = UInt32P(0u);
            StoreMis(ps.arena->misWords.data(), mv, rec0, fill);

            // Sample table row per lane (slot == lane index).
            alignas(64) float fillA[PACKET_WIDTH];
            alignas(64) uint32_t slotA[PACKET_WIDTH], seedA[PACKET_WIDTH],
                sidxA[PACKET_WIDTH];
            enoki::store_unaligned(fillA, select(fill, FloatP(1.f), FloatP(0.f)));
            enoki::store_unaligned(slotA, lane.slot);
            enoki::store_unaligned(seedA, lane.core.seed);
            enoki::store_unaligned(sidxA, lane.core.sampleIdx);
            for (size_t i = 0; i < PACKET_WIDTH; ++i)
            {
                if (fillA[i] <= 0.f)
                    continue;
                const uint32_t s = slotA[i];
                tbl.L[s] = 0.f;
                tbl.alpha[s] = 0.f;
                tbl.seed[s] = seedA[i];
                tbl.sidx[s] = sidxA[i];
            }
        }

        // Acquire a free sample slot per filling lane.
        template <class SamplerT>
        inline bool RefillEyeSched(const WalkCtx &c, PathStore &ps, SampleTable &tbl,
                                   BidirEyeConsumer &cons, const Camera &camera,
                                   const SamplerT &sampler, JobCursor &jobs,
                                   uint64_t sampleBasis, uint32_t nT, bool useT1)
        {
            EyeLanes &lane = cons.Tail();
            if (!tbl.HasFreeSlot())
                return false;
            PixelPlan plan;
            // Admit even a single free lane.
            const uint32_t freeN = uint32_t(tbl.freeList.size());
            if (!PlanPixels(&camera, sampler, jobs, sampleBasis, lane.core.alive, 1,
                            plan, freeN))
                return false;
            const MaskP fill = plan.fill;
            alignas(64) float fillA[PACKET_WIDTH];
            enoki::store_unaligned(fillA, select(fill, FloatP(1.f), FloatP(0.f)));

            // Acquire one slot per filling lane.
            alignas(64) uint32_t slotA[PACKET_WIDTH];
            for (size_t i = 0; i < PACKET_WIDTH; ++i)
                slotA[i] = fillA[i] > 0.f ? tbl.AcquireSlot() : 0u;
            const UInt32P slots = enoki::load_unaligned<UInt32P>(slotA);

            std::vector<uint32_t> taken;
            const UInt32P paths = AllocPaths(ps, fill, taken);

            EyeLanes fresh = EyeLanes::Fresh(plan);
            fresh.path = paths;
            fresh.slot = slots;
            fresh.v = Int32P(1);
            fresh.core.medium = c.cameraMediumID;
            lane = Blend(fill, fresh, lane);

            WriteCameraVertex(ps, c, tbl, lane, fill);

            const FloatP camPdf = useT1 ? camera.Pdf(plan.primary, fill) : FloatP(0.f);
            alignas(64) float xA[PACKET_WIDTH], yA[PACKET_WIDTH], wA[PACKET_WIDTH],
                cwA[PACKET_WIDTH], pdfA[PACKET_WIDTH];
            enoki::store_unaligned(xA, plan.x);
            enoki::store_unaligned(yA, plan.y);
            enoki::store_unaligned(wA, plan.sw.w);
            enoki::store_unaligned(cwA, plan.camWeight);
            enoki::store_unaligned(pdfA, camPdf);
            const uint32_t pend = 1u + nT + (useT1 ? (1u + nT) : 0u);
            for (size_t i = 0; i < PACKET_WIDTH; ++i)
            {
                if (fillA[i] <= 0.f)
                    continue;
                const uint32_t s = slotA[i];
                tbl.x[s] = xA[i];
                tbl.y[s] = yA[i];
                tbl.w[s] = wA[i];
                tbl.camWeight[s] = cwA[i];
                tbl.camPdfR0[s] = pdfA[i];
                tbl.pending[s] = pend;
            }
            return true;
        }

        // Route the work the dead eye path unblocks through the scheduler.
        inline void EyeDone(Scheduler &sched, PathStore &ps, const EyeLanes &lane,
                            const MaskP &dying)
        {
            alignas(64) uint32_t pathA[PACKET_WIDTH], slotA[PACKET_WIDTH];
            alignas(64) float dieA[PACKET_WIDTH], alphaA[PACKET_WIDTH];
            alignas(64) int32_t vi[PACKET_WIDTH];
            enoki::store_unaligned(dieA, select(dying, FloatP(1.f), FloatP(0.f)));
            enoki::store_unaligned(pathA, lane.path);
            enoki::store_unaligned(slotA, lane.slot);
            enoki::store_unaligned(alphaA, lane.alpha);
            enoki::store_unaligned(vi, lane.v);
            for (size_t i = 0; i < PACKET_WIDTH; ++i)
            {
                if (dieA[i] <= 0.f)
                    continue;
                const uint32_t p = pathA[i];
                ps.n[p] = uint32_t(vi[i] < 0 ? 0 : vi[i]);
                // Record the final alpha.
                sched.tbl.alpha[slotA[i]] = alphaA[i];
                // The path stays allocated until the sample retires.
                sched.OnEyeDone(slotA[i], p);
            }
        }

        // Can the eye walker admit a new sample?
        inline bool EyeAdmissible(const SampleTable &tbl, const JobCursor &jobs)
        {
            return tbl.HasFreeSlot() &&
                   !(jobs.spp == 0u || jobs.x1 <= jobs.x0 || jobs.y1 <= jobs.y0);
        }

        // Refill dead lanes, advance live lanes, and route dying paths.
        template <class SamplerT>
        inline bool StepEyeOnce(const WalkCtx &c, PathStore &ps, SampleTable &tbl,
                                BidirEyeConsumer &cons, const Camera &camera,
                                const SamplerT &sampler, JobCursor &jobs,
                                uint64_t sampleBasis, Scheduler &sched,
                                uint32_t nT)
        {
            EyeLanes &lane = cons.Tail();
            // Only fill when a free slot exists.
            if (tbl.HasFreeSlot())
                RefillEyeSched(c, ps, tbl, cons, camera, sampler, jobs, sampleBasis,
                               nT, c.useT1);
            const MaskP before = lane.core.alive;
            if (!any(before))
                return false;
            Event ev = Advance<Dir::Eye>(c, lane.core, cons, lane.core.alive);
            cons.OnEvent(lane.core, ev);
            cons.OnVertex(
                SamplerSource{sampler, lane.core.seed, lane.core.sampleIdx},
                lane.core, ev);
            cons.Survival(lane.core);
            cons.PostVertex(lane.core, ev);
            const MaskP dying = before && !lane.core.alive;
            if (any(dying))
                EyeDone(sched, ps, lane, dying);
            return true;
        }

        // RNG origin base for a light subpath.
        inline uint32_t LightOriginBase(const WalkCtx &c)
        {
            return uint32_t(2 + std::max(0, c.eyeDepth - 1) * 4);
        }
        inline uint32_t AllLightBase(const WalkCtx &c, uint32_t slot)
        {
            return uint32_t(LIGHT_ALL_DIM +
                            slot * uint32_t(6 + 4 * std::max(0, c.lightDepth - 1)));
        }

        // Pick the oldest admissible sample with nextLight<nT,
        // reserve its light list slot, and allocate a path.
        template <class SamplerT>
        inline bool RefillLightSched(const WalkCtx &c, PathStore &ps,
                                     SampleTable &tbl, BidirLightConsumer &cons,
                                     const SamplerT &sampler, uint32_t slot,
                                     Scheduler &sched)
        {
            LightLanes &lane = cons.Tail();
            const MaskP dead = !lane.core.alive;
            if (none(dead))
                return false;

            // Choose one admissible sample per dead lane (nextLight < nT).
            alignas(64) float deadA[PACKET_WIDTH];
            enoki::store_unaligned(deadA, select(dead, FloatP(1.f), FloatP(0.f)));
            uint32_t nDead = 0;
            for (size_t i = 0; i < PACKET_WIDTH; ++i)
                nDead += deadA[i] > 0.f;

            uint32_t chosenS[PACKET_WIDTH];
            uint32_t nChosen = 0;
            for (uint32_t s = 0; s < uint32_t(tbl.seed.size()) && nChosen < nDead;
                 ++s)
            {
                if (tbl.nextLight[s] >= tbl.nT)
                    continue;
                if (tbl.eyePath[s] == LUX_INVALID_PATH && !tbl.eyeDone[s])
                    continue; // sample not admitted yet
                chosenS[nChosen++] = s;
            }
            if (nChosen == 0)
                return false;

            // Map the k-th chosen sample onto the k-th dead lane.
            const bool allKind =
                c.lsP->kind == LightsSamplingStrategy::StrategyKind::All;
            alignas(64) uint32_t sSelA[PACKET_WIDTH];
            alignas(64) float swA[PACKET_WIDTH];
            alignas(64) uint32_t seedA[PACKET_WIDTH], sidxA[PACKET_WIDTH];
            alignas(64) uint32_t slotA[PACKET_WIDTH], loBaseA[PACKET_WIDTH];
            alignas(64) float fillF[PACKET_WIDTH] = {};
            const uint32_t oneLoBase = LightOriginBase(c);
            {
                uint32_t k = 0;
                for (size_t i = 0; i < PACKET_WIDTH; ++i)
                {
                    if (deadA[i] <= 0.f || k >= nChosen)
                        continue;
                    const uint32_t s = chosenS[k++];
                    const uint32_t j = tbl.nextLight[s]; // slot this lane will fill
                    fillF[i] = 1.f;
                    sSelA[i] = s;
                    swA[i] = tbl.w[s];
                    seedA[i] = tbl.seed[s];
                    sidxA[i] = tbl.sidx[s];
                    slotA[i] = allKind ? j : 0u;
                    loBaseA[i] = allKind ? AllLightBase(c, j) : oneLoBase;
                }
            }
            const MaskP fill =
                enoki::load_unaligned<FloatP>(fillF) > FloatP(0.5f);
            const UInt32P loBaseV = enoki::load_unaligned<UInt32P>(loBaseA);
            const UInt32P slotV = enoki::load_unaligned<UInt32P>(slotA);

            FloatP uSel(0.f);
            if (!allKind)
                uSel = sampler.Get1D(enoki::load_unaligned<UInt32P>(seedA),
                                     enoki::load_unaligned<UInt32P>(sidxA),
                                     loBaseV);
            const LightOriginPick lp =
                allKind ? PickLightPerSlot(c, *c.lsP, slotV, fill)
                        : PickLight(c, *c.lsP, 0u, uSel, fill);

            std::vector<uint32_t> taken;
            const UInt32P paths = AllocPaths(ps, fill, taken);

            LightLanes fresh = LightLanes::Fresh();
            fresh.core.alive = fill;
            fresh.core.sw.FromWavelength(enoki::load_unaligned<FloatP>(swA));
            fresh.core.seed = enoki::load_unaligned<UInt32P>(seedA);
            fresh.core.sampleIdx = enoki::load_unaligned<UInt32P>(sidxA);
            fresh.path = paths;
            fresh.sample = enoki::load_unaligned<UInt32P>(sSelA);
            fresh.lpSlot = slotV;
            fresh.loBase = loBaseV;
            lane = Blend(fill, fresh, lane);

            const MaskP ok = cons.BuildOrigin(
                SamplerSource{sampler, lane.core.seed, lane.core.sampleIdx}, slot,
                lp.light, lp.idx, lp.pSel);

            // Path selPdf, owner sample, list index j; and bump sample's nextLight.
            {
                alignas(64) float okA[PACKET_WIDTH], pdfA[PACKET_WIDTH];
                alignas(64) uint32_t pathA[PACKET_WIDTH], sA[PACKET_WIDTH];
                enoki::store_unaligned(okA, select(ok, FloatP(1.f), FloatP(0.f)));
                enoki::store_unaligned(pdfA, lp.pSel);
                enoki::store_unaligned(pathA, lane.path);
                enoki::store_unaligned(sA, lane.sample);
                alignas(64) float fillA[PACKET_WIDTH];
                enoki::store_unaligned(fillA, select(fill, FloatP(1.f), FloatP(0.f)));
                for (size_t i = 0; i < PACKET_WIDTH; ++i)
                {
                    if (fillA[i] <= 0.f)
                        continue;
                    const uint32_t s = sA[i];
                    if (okA[i] > 0.f)
                    {
                        const uint32_t p = pathA[i];
                        ps.selPdf[p] = pdfA[i];
                        ps.owner[p] = s;
                        const uint32_t j = tbl.nextLight[s];
                        ps.lightSlot[p] = j;
                        tbl.SetLightPath(s, j, p);
                        tbl.nextLight[s] = j + 1u;
                    }
                    else
                    {
                        // If a path wasn't in the light list,
                        // free it now or it will never retire.
                        ps.freePaths.push_back(pathA[i]);
                        // This list entry can never spawn,
                        // advance past it.
                        sched.CancelLight(s, tbl.nextLight[s]);
                        tbl.nextLight[s] = tbl.nextLight[s] + 1u;
                    }
                }
            }
            // A filled lane whose origin failed holds no valid path.
            lane.core.alive = lane.core.alive && (!fill || ok);
            return any(ok);
        }

        // Record n[path], route work the drained light path unblocks, free slot.
        inline void LightDone(Scheduler &sched, PathStore &ps,
                              const LightLanes &lane, const MaskP &dying)
        {
            alignas(64) uint32_t pathA[PACKET_WIDTH], sA[PACKET_WIDTH];
            alignas(64) float dieA[PACKET_WIDTH];
            alignas(64) int32_t vi[PACKET_WIDTH];
            enoki::store_unaligned(dieA, select(dying, FloatP(1.f), FloatP(0.f)));
            enoki::store_unaligned(pathA, lane.path);
            enoki::store_unaligned(sA, lane.sample);
            enoki::store_unaligned(vi, lane.v);
            for (size_t i = 0; i < PACKET_WIDTH; ++i)
            {
                if (dieA[i] <= 0.f)
                    continue;
                const uint32_t p = pathA[i];
                ps.n[p] = uint32_t(vi[i] < 0 ? 0 : vi[i]);
                // The light path stays allocated until its sample retires.
                sched.OnLightDone(sA[i], p, ps.lightSlot[p]);
            }
        }

        // One light bounce step for the independent scheduler.
        template <class SamplerT>
        inline bool StepLightOnce(const WalkCtx &c, PathStore &ps, SampleTable &tbl,
                                  BidirLightConsumer &cons,
                                  const SamplerT &sampler, uint32_t slot,
                                  Scheduler &sched)
        {
            LightLanes &lane = cons.Tail();
            RefillLightSched(c, ps, tbl, cons, sampler, slot, sched);
            const MaskP before = lane.core.alive;
            if (!any(before))
                return false;
            Event ev = Advance<Dir::Light>(c, lane.core, cons, lane.core.alive);
            cons.OnEvent(lane.core, ev);
            cons.OnVertex(
                SamplerSource{sampler, lane.core.seed, lane.core.sampleIdx},
                lane.core, ev);
            cons.Survival(lane.core);
            cons.PostVertex(lane.core, ev);
            const MaskP dying = before && !lane.core.alive;
            if (any(dying))
                LightDone(sched, ps, lane, dying);
            return true;
        }

    } // namespace wf
} // namespace lux2

#endif // LUX2_INTEGRATORS_WF_WALKERS_H
