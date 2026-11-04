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

    void BidirIntegrator::Start(const Scene &scene)
    {
        m_scene = &scene;

        // Build the light sampling tables.
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

        // Scene derived parts.
        WalkCtx ctx;
        {
            TileCtxOptions opts;
            opts.prebuiltD = &m_lsD;
            opts.prebuiltP = &m_lsP;
            opts.directStrategy = &m_lightStrategy;
            opts.pathStrategy = &m_lightPathStrategy;
            BuildTileCtx(scene, opts, ctx);
        }
        ctx.camera = &camera;
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
        // Only the first tile runs the density check.
        ctx.checkDensity = m_debug && !m_densityChecked.exchange(true);
        BidirDiagLocal dl;
        if (m_diagStore)
        {
            dl.Init(m_eyeDepth, m_lightDepth);
            ctx.diag = &dl;
        }
#endif

        // Light-path task count is one under `one`/`auto`, per task under `all`.
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

        wf::BlockArena arenaEye, arenaLight;
        wf::PathStore psEye, psLight;
        psEye.Init(&arenaEye, kNs);
        psLight.Init(&arenaLight, kLightPool);
        wf::SampleTable tbl;
        tbl.Init(kNs, nT);

        wf::BidirEyeConsumer eyeCons(ctx, &psEye, &tbl);
        wf::BidirLightConsumer lightCons(ctx, &psLight);
        wf::Scheduler sched(tbl, psEye, psLight, ctx.useT1);

        JobCursor jobs(tile, spp);
        Film &film = scene.GetFilm();

        // Drain completed samples.
        auto flushRetire = [&]()
        {
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

        // Run one connector packet from a ready queue.
        auto runNee = [&]() -> bool
        {
            UInt32P sample;
            const uint32_t n = sched.nee.FillPacket(sample);
            if (n == 0)
                return false;
            const MaskP act = fillMask(n);
            wf::NeeConnector neeC(ctx, sampler, tbl, psEye);
            neeC.ePath =
                enoki::gather<UInt32P>(tbl.eyePath.data(), sample, act);
            neeC.sample = sample;
            neeC.nE =
                enoki::gather<Int32P>(sched.eyeN.data(), neeC.ePath, act);
            neeC.t = Int32P(2);
            neeC.alive = act && (neeC.nE >= Int32P(2));
            while (any(neeC.alive))
                neeC.Step();
            for (uint32_t i = 0; i < n; ++i)
                sched.CompleteSample(sample, i);
            return true;
        };

        auto runCamNee = [&]() -> bool
        {
            UInt32P sample;
            const uint32_t n = sched.camNee.FillPacket(sample);
            if (n == 0)
                return false;
            const MaskP act = fillMask(n);
            wf::CamNeeConnector cnee(ctx, sampler, tbl, psEye, &film);
            cnee.ePath =
                enoki::gather<UInt32P>(tbl.eyePath.data(), sample, act);
            cnee.sample = sample;
            cnee.alive = act;
            cnee.Run();
            for (uint32_t i = 0; i < n; ++i)
                sched.CompleteSample(sample, i);
            return true;
        };

        // One Gen packet from the fullest bin.
        auto runGen = [&]() -> bool
        {
            const int bin = sched.gen.FullestBin();
            if (bin < 0)
                return false;
            UInt32P ePath, lPath, sample, slot;
            const uint32_t n =
                sched.gen.FillPacket(bin, ePath, lPath, sample, slot);
            if (n == 0)
                return false;
            const MaskP act = fillMask(n);
            wf::GenConnector gen(ctx, tbl, psEye, psLight, 0u);
            gen.ePath = ePath;
            gen.lPath = lPath;
            gen.sample = sample;
            gen.nE = enoki::gather<Int32P>(sched.eyeN.data(), ePath, act);
            gen.nL = enoki::gather<Int32P>(psLight.n.data(), lPath, act);
            gen.t = Int32P(2);
            gen.s = Int32P(1);
            gen.alive = act;
            while (any(gen.alive))
                gen.Step();
            for (uint32_t i = 0; i < n; ++i)
                sched.CompleteSample(sample, i);
            return true;
        };

        auto runCam = [&]() -> bool
        {
            UInt32P lPath;
            const uint32_t n = sched.cam.FillPacket(lPath);
            if (n == 0)
                return false;
            const MaskP act = fillMask(n);
            UInt32P sample =
                enoki::gather<UInt32P>(psLight.owner.data(), lPath, act);
            wf::CamConnector cam(ctx, tbl, psEye, psLight, 0u, &film);
            cam.ePath =
                enoki::gather<UInt32P>(tbl.eyePath.data(), sample, act);
            cam.lPath = lPath;
            cam.sample = sample;
            cam.nL = enoki::gather<Int32P>(psLight.n.data(), lPath, act);
            cam.s = Int32P(1);
            cam.alive = act && (cam.nL >= Int32P(1));
            while (any(cam.alive))
                cam.Step();
            for (uint32_t i = 0; i < n; ++i)
                sched.CompleteSample(sample, i);
            return true;
        };

        // Main loop.
        for (;;)
        {
            // 1. Retire any completed samples first, then free.
            if (!sched.retireQueue.empty())
            {
                flushRetire();
                continue;
            }
            // 2. Fullest queue first.
            if (runNee() || runCamNee() || runGen() || runCam())
                continue;
            // 3. Pressure on queued pairs.
            const bool admit = sched.gen.Queued() < highWater;
            if (admit && wf::StepEyeOnce(ctx, psEye, tbl, eyeCons, camera,
                                         sampler, jobs, sampleBasis, sched, nT))
                continue;
            if (admit &&
                wf::StepLightOnce(ctx, psLight, tbl, lightCons, sampler, 0u,
                                  sched))
                continue;
            // 4. Terminate, no live walkers!
            const bool eyeLive = any(eyeCons.Tail().core.alive);
            const bool lightLive = any(lightCons.Tail().core.alive);
            const bool pendingWork =
                !sched.nee.Empty() || !sched.camNee.Empty() ||
                !sched.gen.Empty() || !sched.cam.Empty() ||
                !sched.retireQueue.empty();
            if (!eyeLive && !lightLive && !pendingWork)
                break;
            // Starvation guard.
            if (wf::StepEyeOnce(ctx, psEye, tbl, eyeCons, camera, sampler, jobs,
                                sampleBasis, sched, nT) ||
                wf::StepLightOnce(ctx, psLight, tbl, lightCons, sampler, 0u,
                                  sched))
                continue;
            break;
        }
        // The loop only exits once retireQueue is drained.
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
        // Wavefront pool sizing.
        const int wfSamples = p ? p->FindOneInt("wf_samples", 128) : 128;
        const int wfLightPool = p ? p->FindOneInt("wf_lightpool", 0) : 0;
        const int wfHighWater = p ? p->FindOneInt("wf_highwater", 0) : 0;
        if (p)
            p->EraseString("mismode");
        return std::make_shared<BidirIntegrator>(
            eyeDepth, lightDepth, maxDepth, eyeRR, lightRR, useT1, debug, cfg,
            lightStrategy, lightPathStrategy, wfSamples, wfLightPool,
            wfHighWater);
    }

    LUX2_REGISTER_SURFACE_INTEGRATOR(BidirIntegrator, "bidirectional");

} // namespace lux2