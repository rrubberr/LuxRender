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
#include "integrators/consumers/path_consumer.h"
#include "integrators/path.h"

#include "integrators/lane/jobs.h"
#include "integrators/lane/keys.h"
#include "integrators/lane/lane_core.h"
#include "integrators/lane/retire.h"
#include "integrators/lane/sampler_source.h"
#include "integrators/lane/tile_ctx.h"

#include "core/dynload.h"
#include "core/register.h"
#include "core/scene.h"

#include <enoki/array.h>

namespace lux2
{
    void PathIntegrator::Start(const Scene &scene)
    {
        m_scene = &scene;
        // Build light sampling tables once per group.
        m_ls.clear();
        for (int g = 0; g < scene.LightGroupCount(); ++g)
            m_ls.push_back(CreateLightsSamplingStrategy(m_lightStrategy,
                                                        scene.GroupLights(g), g, scene));
    }

    void PathIntegrator::RenderTile(const Scene &scene, const Tile &tile,
                                    Film &dest, Sampler &sampler,
                                    IntegratorWorkerState *ws,
                                    uint64_t sampleBasis, uint32_t spp)
    {
        (void)ws; // Path keeps no persistent worker state.
        const Camera &camera = scene.GetCamera();
        OccupancyBegin();

        // Path integrator switches.
        TileCtx ctx;
        {
            TileCtxOptions opts;
            opts.prebuiltD = &m_ls;
            opts.directStrategy = &m_lightStrategy;
            BuildTileCtx(scene, opts, ctx);
        }
        PathConfig cfg;
        cfg.FULL_MIS = (m_lightMode == LightMode::MIS);
        cfg.NEE = (m_lightMode != LightMode::BSDF) && m_directLightSampling;
        cfg.includeEnv = m_includeEnvironment;
        cfg.maxDepth = m_maxDepth;
        cfg.rrEfficiency = (m_rrStrategy == "efficiency");
        cfg.rrProb = FloatP(m_rrContinueProb);

        PathConsumer cons(ctx, cfg);
        PathLanes &st = cons.Tail();
        // All lanes start dead.
        st.core.alive = MaskP(false);
        st.prevIsMedium = MaskP(false);
        st.prevRecvHasN = MaskP(true);

        JobCursor jobs(tile, spp);
        RetireBuffer retire;

        RefillLanes(st, &camera, sampler, jobs, sampleBasis, 1);
        while (any(st.core.alive))
        {
            OccupancyStep(enoki::count(st.core.alive));
            const MaskP before = st.core.alive;

            // One transport step advance, estimator hooks.
            Event ev = Advance<Dir::Eye>(ctx, st.core, cons, st.core.alive);
            cons.OnEvent(st.core, ev);
            cons.OnVertex(SamplerSource{sampler, st.core.seed, st.core.sampleIdx},
                          st.core, ev);
            cons.Survival(st.core);
            cons.PostVertex(st.core, ev);

            // Paths that retired this bounce.
            const MaskP finished = before && !st.core.alive;
            if (any(finished))
            {
                retire.Push({st.x, st.y, st.alpha, st.camWeight, st.L,
                             st.core.sw.w},
                            finished);
                retire.Flush(dest, false);
            }
            // Refill the packet from the job queue.
            RefillLanes(st, &camera, sampler, jobs, sampleBasis);
        }
        retire.Flush(dest, true);
        OccupancyTileEnd("path", tile.x0, tile.y0, unsigned(PACKET_WIDTH));
    }

    std::shared_ptr<SurfaceIntegrator> PathIntegrator::CreateSurfaceIntegrator(
        const PluginContext &ctx)
    {
        ParamSet *p = ctx.params;
        int maxDepth = p ? p->FindOneInt("maxdepth", 8) : 8;
        if (maxDepth > MAX_DEPTH)
        {
            LOG(LUX_ERROR, LUX_SYNTAX)
                << "Path: maxdepth capped at " << MAX_DEPTH
                << " (requested " << maxDepth << ")";
            maxDepth = MAX_DEPTH;
        }
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
