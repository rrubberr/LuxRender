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
#include "integrators/lane/blend.h"
#include "integrators/lane/direct.h"
#include "integrators/lane/geom.h"
#include "integrators/lane/jobs.h"
#include "integrators/lane/keys.h"
#include "integrators/lane/light_pick.h"
#include "integrators/lane/math.h"
#include "integrators/lane/occlusion.h"
#include "integrators/lane/retire.h"
#include "integrators/lane/sampler_source.h"
#include "integrators/lane/tile_ctx.h"
#include "integrators/wf/connectors.h"
#include "integrators/wf/sample_table.h"
#include "integrators/wf/storage.h"
#include "integrators/wf/types.h"
#include "integrators/wf/walkers.h"

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
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace lux2
{

    BidirIntegrator::~BidirIntegrator() = default;

    namespace
    {
        // wf_minfill: -1 = a full packet, otherwise clamped to [1, width].
        uint32_t ResolveMinFill(int v)
        {
            if (v < 0)
                return uint32_t(PACKET_WIDTH);
            return std::min<uint32_t>(uint32_t(std::max(v, 1)),
                                      uint32_t(PACKET_WIDTH));
        }
    } // namespace

    void BidirIntegrator::Start(const Scene &scene)
    {
        m_scene = &scene;

        // Build the light sampling tables.
        m_lsD.clear();
        m_lsP.clear();
        const int tableGroups = std::max(1, scene.LightGroupCount());
        for (int g = 0; g < tableGroups; ++g)
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

    namespace
    {
        // Persistent worker wavefront storage.
        struct BidirWorkerState : IntegratorWorkerState
        {
            wf::BlockArena arenaEye, arenaLight;
            wf::PathStore psEye, psLight;
            // Scene derived context is only rebuilt when the key changes.
            WalkCtx ctx;
            const Scene *ctxScene = nullptr;
            int ctxGroup = -1;
        };
    } // namespace

    std::unique_ptr<IntegratorWorkerState>
    BidirIntegrator::NewWorkerState() const
    {
        return std::make_unique<BidirWorkerState>();
    }

    void BidirIntegrator::RenderTile(const Scene &scene, const Tile &tile,
                                     Film &dest, Sampler &sampler,
                                     IntegratorWorkerState *ws,
                                     uint64_t sampleBasis, uint32_t spp)
    {
        const Camera &camera = scene.GetCamera();
        OccupancyBegin();

        // Scene derived data is cached per worker until the scene or active
        // light group changes.
        BidirWorkerState &state = *static_cast<BidirWorkerState *>(ws);
        WalkCtx &ctx = state.ctx;
        if (state.ctxScene != &scene || state.ctxGroup != scene.GetActiveGroup())
        {
            TileCtxOptions opts;
            opts.prebuiltD = &m_lsD;
            opts.prebuiltP = &m_lsP;
            BuildTileCtx(scene, opts, ctx);
            state.ctxScene = &scene;
            state.ctxGroup = scene.GetActiveGroup();
        }
        ctx.camera = &camera;
        // Medium of the camera origin (id 0 = ambient composite when
        // camera in vacuum).
        ctx.cameraMediumID = UInt32P(scene.CameraMediumID());
        ctx.eyeDepth = m_eyeDepth;
        ctx.lightDepth = m_lightDepth;
        ctx.eyeRR = FloatP(m_eyeRRThreshold);
        ctx.lightRR = FloatP(m_lightRRThreshold);
        // Accumulators merged into the store at tile end.
        ctx.cfg = m_diagCfg;
#ifdef LUX2_INTEGRATOR_DEBUG
        {
            // Record the configured depths.
            static std::atomic<bool> s_depthLogged{false};
            if (!s_depthLogged.exchange(true))
                fprintf(stderr,
                        "[WFSTAT] config eyeDepth=%d lightDepth=%d "
                        "eyeRR=%.3f lightRR=%.3f wfSamples=%d minFill=%u\n",
                        m_eyeDepth, m_lightDepth, m_eyeRRThreshold,
                        m_lightRRThreshold, m_wfSamples,
                        ResolveMinFill(m_wfMinFill));
        }
        // Only the first tile runs the density check.
        ctx.checkDensity = m_debug && !m_densityChecked.exchange(true);
        BidirDiagLocal dl;
        if (m_diagStore)
        {
            dl.Init(m_eyeDepth, m_lightDepth);
            ctx.diag = &dl;
        }
#endif

        // Lightpath task count is one under `one`/`auto`, per task under `all`.
        const uint32_t nT =
            (ctx.lsP->kind == LightsSamplingStrategy::StrategyKind::All)
                ? ctx.lsP->TaskCount()
                : 1u;

        // Sample pool capacity Ns.
        const uint32_t kNs = uint32_t(m_wfSamples);
        const uint32_t kLightPool =
            m_wfLightPool > 0 ? uint32_t(m_wfLightPool) : kNs * nT;
        const uint32_t highWater =
            m_wfHighWater > 0 ? uint32_t(m_wfHighWater) : 2u * kNs;
        const uint32_t minFill = ResolveMinFill(m_wfMinFill);

        // Persistent worker arenas (state/ctx resolved above).
        wf::BlockArena &arenaEye = state.arenaEye;
        wf::BlockArena &arenaLight = state.arenaLight;
        wf::PathStore &psEye = state.psEye;
        wf::PathStore &psLight = state.psLight;
        psEye.Init(&arenaEye, kNs);
        psLight.Init(&arenaLight, kLightPool);
        wf::SampleTable tbl;
        tbl.Init(kNs, nT);

        // [t][s] -> MIS bin lookup for the flattened connection queues.
        wf::BidirEyeConsumer eyeCons(ctx, &psEye, &tbl);
        wf::BidirLightConsumer lightCons(ctx, &psLight);
        wf::Scheduler sched(tbl, psEye, psLight, m_binTable);
        if (ctx.cfg.wfSortByPath)
        {
            sched.gen.SetSortByPath(true);
            sched.nee.SetSortByPath(true);
            sched.camNee.SetSortByPath(true);
            sched.cam.SetSortByPath(true);
        }

        JobCursor jobs(tile, spp);

        // Drain completed samples.
        auto flushRetire = [&]()
        {
            WfTimer wt(WF_PH_RETIRE);
            float fx[PACKET_WIDTH], fy[PACKET_WIDTH], fL[PACKET_WIDTH];
            float falpha[PACKET_WIDTH], fw[PACKET_WIDTH], fwt[PACKET_WIDTH];
            const size_t n = sched.retireQueue.size();
            for (size_t base = 0; base < n; base += PACKET_WIDTH)
            {
                const uint32_t cnt =
                    static_cast<uint32_t>(std::min<size_t>(PACKET_WIDTH, n - base));
                for (uint32_t i = 0; i < PACKET_WIDTH; ++i)
                {
                    if (i >= cnt)
                    {
                        fx[i] = fy[i] = fL[i] = falpha[i] = 0.f;
                        fw[i] = 0.f; // padding lane
                        fwt[i] = 650.f;
                        continue;
                    }
                    const uint32_t s = sched.retireQueue[base + i];
                    const float Lv = tbl.L[s];
                    fx[i] = tbl.x[s];
                    fy[i] = tbl.y[s];
                    fL[i] = enoki::isfinite(Lv) ? Lv : 0.f;
                    falpha[i] = tbl.alpha[s];
                    fw[i] = tbl.camWeight[s];
                    fwt[i] = tbl.w[s];
                }
                SpectrumWavelengthsP sw;
                sw.FromWavelength(enoki::load_unaligned<FloatP>(fwt));
                dest.Splat(enoki::load_unaligned<FloatP>(fx),
                           enoki::load_unaligned<FloatP>(fy),
                           SWCSpectrumP(enoki::load_unaligned<FloatP>(fL)), sw,
                           enoki::load_unaligned<FloatP>(falpha),
                           enoki::load_unaligned<FloatP>(fw));
                WfStatSplatted(int(cnt));
            }
            // pending==0 when every connector finished reading this sample's paths.
            for (uint32_t s : sched.retireQueue)
            {
                if (tbl.eyePath[s] != wf::LUX_INVALID_PATH)
                    psEye.freePaths.push_back(tbl.eyePath[s]);
                for (uint32_t j = 0; j < nT; ++j)
                {
                    const uint32_t lp = tbl.LightPath(s, j);
                    if (lp != wf::LUX_INVALID_PATH)
                        psLight.freePaths.push_back(lp);
                }
                tbl.ReleaseSlot(s);
            }
            sched.retireQueue.clear();
        };

        // Mask of the first n lanes.
        auto fillMask = [](uint32_t n)
        {
            alignas(64) uint32_t a[PACKET_WIDTH];
            for (uint32_t i = 0; i < PACKET_WIDTH; ++i)
                a[i] = i;
            return enoki::load_unaligned<UInt32P>(a) < UInt32P(n);
        };

        // Store a packet's sample ids so each lane can settle its token.
        auto completeItems = [&](const UInt32P &sample, uint32_t n)
        {
            WfTimer st(WF_PH_SCHED);
            alignas(64) uint32_t sA[PACKET_WIDTH];
            enoki::store_unaligned(sA, sample);
            for (uint32_t i = 0; i < n; ++i)
                sched.CompleteItem(sA[i]);
        };

        // Run one connector packet from a ready queue.
        auto runNee = [&](uint32_t need, bool topUp) -> bool
        {
            UInt32P ePath, lPath, sample, slot;
            Int32P t, s;
            uint32_t n;
            {
                WfTimer st(WF_PH_SCHED);
                const int bin = sched.nee.FullestBin();
                if (bin < 0 || (!topUp && sched.nee.BinSize(bin) < need))
                    return false;
                n = sched.nee.Fill(bin, ePath, lPath, sample, slot, t, s, topUp);
            }
            if (n == 0)
                return false;
            WfStatDispatch(WF_Q_NEE, int(n), n < uint32_t(PACKET_WIDTH));
            WfTimer wt(WF_PH_NEE);
            const MaskP act = fillMask(n);
            wf::NeeConnector neeC(ctx, sampler, tbl, psEye);
            neeC.ePath = ePath;
            neeC.sample = sample;
            neeC.nE =
                enoki::gather<Int32P>(sched.eyeN.data(), ePath, act);
            neeC.t = t;
            neeC.alive = act && (neeC.nE >= Int32P(2));
            neeC.Run();
            completeItems(sample, n);
            return true;
        };

        auto runCamNee = [&](uint32_t need, bool topUp) -> bool
        {
            UInt32P ePath, lPath, sample, slot;
            Int32P t, s;
            uint32_t n;
            {
                WfTimer st(WF_PH_SCHED);
                const int bin = sched.camNee.FullestBin();
                if (bin < 0 || (!topUp && sched.camNee.BinSize(bin) < need))
                    return false;
                n =
                    sched.camNee.Fill(bin, ePath, lPath, sample, slot, t, s, topUp);
            }
            if (n == 0)
                return false;
            WfStatDispatch(WF_Q_CAMNEE, int(n), n < uint32_t(PACKET_WIDTH));
            WfTimer wt(WF_PH_CAMNEE);
            const MaskP act = fillMask(n);
            wf::CamNeeConnector cnee(ctx, sampler, tbl, psEye, &dest);
            cnee.ePath = ePath;
            cnee.sample = sample;
            cnee.alive = act;
            cnee.Run();
            completeItems(sample, n);
            return true;
        };

        // One Gen packet: lanes are (t,s) items from the fullest bin.
        auto runGen = [&](uint32_t need, bool topUp) -> bool
        {
            UInt32P ePath, lPath, sample, slot;
            Int32P t, s;
            uint32_t n;
            {
                WfTimer st(WF_PH_SCHED);
                const int bin = sched.gen.FullestBin();
                if (bin < 0 || (!topUp && sched.gen.BinSize(bin) < need))
                    return false;
                n = sched.gen.Fill(bin, ePath, lPath, sample, slot, t, s, topUp);
            }
            if (n == 0)
                return false;
            WfStatDispatch(WF_Q_GEN, int(n), n < uint32_t(PACKET_WIDTH));
            WfTimer wt(WF_PH_GEN);
            const MaskP act = fillMask(n);
            wf::GenConnector gen(ctx, tbl, psEye, psLight);
            gen.ePath = ePath;
            gen.lPath = lPath;
            gen.sample = sample;
            gen.slot = slot;
            gen.nE = enoki::gather<Int32P>(sched.eyeN.data(), ePath, act);
            gen.nL = enoki::gather<Int32P>(psLight.n.data(), lPath, act);
            gen.t = t;
            gen.s = s;
            gen.alive = act;
            gen.Run();
            completeItems(sample, n);
            return true;
        };

        auto runCam = [&](uint32_t need, bool topUp) -> bool
        {
            UInt32P ePath, lPath, sample, slot;
            Int32P t, s;
            uint32_t n;
            {
                WfTimer st(WF_PH_SCHED);
                const int bin = sched.cam.FullestBin();
                if (bin < 0 || (!topUp && sched.cam.BinSize(bin) < need))
                    return false;
                n = sched.cam.Fill(bin, ePath, lPath, sample, slot, t, s, topUp);
            }
            if (n == 0)
                return false;
            WfStatDispatch(WF_Q_CAM, int(n), n < uint32_t(PACKET_WIDTH));
            WfTimer wt(WF_PH_CAM);
            const MaskP act = fillMask(n);
            wf::CamConnector cam(ctx, tbl, psEye, psLight, &dest);
            // Cam items carry only the sample.
            cam.ePath = enoki::gather<UInt32P>(tbl.eyePath.data(), sample, act);
            cam.lPath = lPath;
            cam.sample = sample;
            cam.slot = slot;
            cam.nL = enoki::gather<Int32P>(psLight.n.data(), lPath, act);
            cam.s = s;
            cam.alive = act && (cam.nL >= Int32P(1));
            cam.Run();
            completeItems(sample, n);
            return true;
        };

        // Alternate the walkers so neither starves.
        bool eyeFirst = true;
        auto stepWalkers = [&]() -> bool
        {
            for (int k = 0; k < 2; ++k)
            {
                const bool eye = (k == 0) == eyeFirst;
                const bool ran =
                    eye ? wf::StepEyeOnce(ctx, psEye, tbl, eyeCons, camera,
                                          sampler, jobs, sampleBasis, sched, nT)
                        : wf::StepLightOnce(ctx, psLight, tbl, lightCons,
                                            sampler, 0u, sched);
                if (ran)
                {
                    eyeFirst = !eye; // the other walker goes first next time
                    return true;
                }
            }
            return false;
        };

        // Stall flush.
        auto runLargest = [&]() -> bool
        {
            const uint32_t sz[4] = {sched.nee.Size(), sched.camNee.Size(),
                                    sched.gen.Size(), sched.cam.Size()};
            int best = -1;
            uint32_t bestN = 0;
            for (int q = 0; q < 4; ++q)
            {
                if (sz[q] > bestN)
                {
                    bestN = sz[q];
                    best = q;
                }
            }
            switch (best)
            {
            case 0:
                return runNee(1, true);
            case 1:
                return runCamNee(1, true);
            case 2:
                return runGen(1, true);
            case 3:
                return runCam(1, true);
            default:
                return false;
            }
        };

#ifdef LUX2_INTEGRATOR_DEBUG
        // Classify every sample slot by lifecycle state.
        auto census = [&]()
        {
            int eyeWalk = 0, waitLight = 0, lightWalk = 0, pendOnly = 0;
            for (uint32_t s = 0; s < kNs; ++s)
            {
                if (tbl.pending[s] == 0u)
                    continue;
                if (!tbl.eyeDone[s])
                {
                    ++eyeWalk;
                    continue;
                }
                bool lWalk = false;
                for (uint32_t j = 0; j < nT; ++j)
                    if (tbl.LightPath(s, j) != wf::LUX_INVALID_PATH &&
                        !tbl.LightDone(s, j))
                        lWalk = true;
                if (lWalk)
                    ++lightWalk;
                else if (tbl.nextLight[s] < tbl.nT)
                    ++waitLight;
                else
                    ++pendOnly;
            }
            WfStatCensus(int(tbl.freeList.size()), eyeWalk, waitLight,
                         lightWalk, pendOnly, int(sched.retireQueue.size()),
                         int(sched.nee.Size()), int(sched.camNee.Size()),
                         int(sched.gen.Size()), int(sched.cam.Size()));
        };
        uint64_t loopIter = 0;
#endif

        // Main loop.
        WfTimer loopTimer(WF_PH_LOOP);
        for (;;)
        {
#ifdef LUX2_INTEGRATOR_DEBUG
            if ((loopIter++ & 1023u) == 0u)
                census();
#endif
            // Retire a full packet's worth at once.
            if (sched.retireQueue.size() >= size_t(PACKET_WIDTH))
            {
                flushRetire();
                WfStatLoopStep(WF_LOOP_RETIRE);
                continue;
            }
            // Dispatch once a single bin holds minFill items.
            if (runNee(minFill, false) || runCamNee(minFill, false) ||
                runGen(minFill, false) || runCam(minFill, false))
            {
                WfStatLoopStep(WF_LOOP_DISPATCH);
                continue;
            }
            // Feed the queues.
            const bool admit = sched.gateSamples < highWater;
            if (admit && stepWalkers())
            {
                WfStatLoopStep(WF_LOOP_WALKER);
                continue;
            }
            // Walkers cannot progress!
            if (runLargest())
            {
                WfStatLoopStep(WF_LOOP_FLUSH);
                continue;
            }
            // Nothing else could run! Drain a partial retire batch!
            if (!sched.retireQueue.empty())
            {
                flushRetire();
                WfStatLoopStep(WF_LOOP_RETIRE);
                continue;
            }
            // 5. Terminate, no live walkers and no pending work!
            const bool eyeLive = any(eyeCons.Tail().core.alive);
            const bool lightLive = any(lightCons.Tail().core.alive);
            const bool pendingWork =
                !sched.nee.Empty() || !sched.camNee.Empty() ||
                !sched.gen.Empty() || !sched.cam.Empty() ||
                !sched.retireQueue.empty();
            if (!eyeLive && !lightLive && !pendingWork)
                break;
            // Step the walkers regardless of `admit`.
            if (stepWalkers())
            {
                WfStatLoopStep(WF_LOOP_STARVE);
                continue;
            }
            break;
        }
        // The loop only exits once retireQueue is drained.
#ifdef LUX2_INTEGRATOR_DEBUG
        // Any slot still pending at exit was stranded by a lost completion.
        {
            int leaked = 0;
            for (uint32_t s = 0; s < kNs; ++s)
                if (tbl.pending[s] != 0u)
                    ++leaked;
            WfStatLeakedSlots(leaked);
        }
#endif
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
        int eyeDepth = p ? p->FindOneInt("eyedepth", 8) : 8;
        int lightDepth = p ? p->FindOneInt("lightdepth", 8) : 8;
        // RNG stride and vertex record layout are sized for MAX_DEPTH.
        if (eyeDepth > MAX_DEPTH || lightDepth > MAX_DEPTH)
        {
            LOG(LUX_ERROR, LUX_SYNTAX)
                << "Bidirectional: eyedepth/lightdepth capped at " << MAX_DEPTH
                << " (requested " << eyeDepth << "/" << lightDepth << ")";
            eyeDepth = std::min(eyeDepth, MAX_DEPTH);
            lightDepth = std::min(lightDepth, MAX_DEPTH);
        }
        const int maxDepth = p ? p->FindOneInt("maxdepth", -1) : -1; // TODO: unused!
        const float eyeRR = p ? p->FindOneFloat("eyerrthreshold", 0.f) : 0.f;
        const float lightRR = p ? p->FindOneFloat("lightrrthreshold", 0.f) : 0.f;
        const bool debug = p ? p->FindOneBool("debug", false) : false;
        DiagConfig cfg;
        cfg.onlyS = p ? p->FindOneInt("onlys", -1) : -1;
        cfg.onlyT = p ? p->FindOneInt("onlyt", -1) : -1;
        cfg.noMis = p ? p->FindOneBool("nomis", false) : false;
        cfg.misPower = p ? p->FindOneFloat("mispower", 2.f) : 2.f;
        cfg.diag = p ? p->FindOneBool("diag", false) : false;
        cfg.threshold = p ? p->FindOneFloat("diagthreshold", 0.f) : 0.f;
        cfg.wfSortByPath = p ? p->FindOneBool("wf_sortpath", false) : false;
        const std::string lightStrategy =
            p ? p->FindOneString("lightstrategy", "one") : "one";
        const std::string lightPathStrategy =
            p ? p->FindOneString("lightpathstrategy", "one") : "one";
        // Wavefront pool sizing.
        const int wfSamples = p ? p->FindOneInt("wf_samples", 128) : 128;
        const int wfLightPool = p ? p->FindOneInt("wf_lightpool", 0) : 0;
        const int wfHighWater = p ? p->FindOneInt("wf_highwater", 0) : 0;
        // -1 = dispatch full packets only.
        const int wfMinFill = p ? p->FindOneInt("wf_minfill", -1) : -1;
        if (p)
            p->EraseString("mismode");
        return std::make_shared<BidirIntegrator>(
            eyeDepth, lightDepth, maxDepth, eyeRR, lightRR, debug, cfg,
            lightStrategy, lightPathStrategy, wfSamples, wfLightPool,
            wfHighWater, wfMinFill);
    }

    LUX2_REGISTER_SURFACE_INTEGRATOR(BidirIntegrator, "bidirectional");

} // namespace lux2