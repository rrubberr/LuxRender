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

#include "renderers/samplerrenderer.h"

#include "core/error.h"
#include "core/film.h"
#include "core/integrator.h"
#include "core/pixelsampler.h"
#include "core/register.h"
#include "core/sampler.h"
#include "core/schedulerpolicy.h"
#include "core/scene.h"
#include "core/threadpool.h"
#include "core/tilequeue.h"

#include <tbb/enumerable_thread_specific.h>
#include <tbb/parallel_for.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>

namespace lux2
{

    namespace
    {
        // Worker state.
        struct WorkerState
        {
            std::unique_ptr<Sampler> sampler;
            std::unique_ptr<PixelSampler> pixelSampler;
            std::unique_ptr<IntegratorWorkerState> iws;
        };
    } // namespace

    void SamplerRenderer::Render(const Scene &scene, SurfaceIntegrator &integrator)
    {
        m_state.store(RenderState::Run);

        Film &film = scene.GetFilm();
        const Sampler &protoSampler = scene.GetSampler();
        PixelSampler &pixelSampler = scene.GetPixelSampler();

        RenderThreadPool &pool = RenderThreadPool::Get();
        if (pool.Count() == 0)
            pool.Init(0); // default == hardware_concurrency
        const uint32_t nWorkers = std::max(1u, pool.Count());

        if (protoSampler.IsCorrelated())
            LOG(LUX_WARNING, LUX_NOERROR)
                << "SamplerRenderer: correlated sampler in use; the atomic-splat "
                   "sink is not implemented yet. Proceeding with per-pixel claims.";

        // One work item is one pixel.
        const uint32_t batch = std::max(1u, protoSampler.SampleCount());
        const Tile fullTile{film.XStart(), film.YStart(),
                            film.XStart() + film.XCount(),
                            film.YStart() + film.YCount()};
        const uint32_t totalPixels = pixelSampler.TotalPixels(fullTile);
        if (totalPixels == 0)
            return;

        const int haltSpp = film.HaltSpp();
        const int haltTime = film.HaltTime();
        const auto startTime = std::chrono::steady_clock::now();

        integrator.Start(scene);

        // Build each worker slot by cloning the sampler.
        const Sampler *protoSamplerPtr = &protoSampler;
        const PixelSampler *protoPsPtr = &pixelSampler;
        auto makeWorker = [protoSamplerPtr, protoPsPtr]
        {
            WorkerState ws;
            ws.sampler = protoSamplerPtr->Clone();
            ws.pixelSampler = protoPsPtr->Clone();
            return ws;
        };
        tbb::enumerable_thread_specific<WorkerState> ets(makeWorker);

        // Repeat passes until every pixel reaches haltSpp (or haltTime).
        std::atomic<uint32_t> cursor{0};
        uint64_t pass = 0;
        while (true)
        {
            if (m_state.load() == RenderState::Terminate)
                break;
            if (haltSpp > 0 && double(pass) * double(batch) >= double(haltSpp))
                break;

            cursor.store(0, std::memory_order_relaxed);
            const uint64_t sampleBasis = pass * uint64_t(batch);
            std::atomic<bool> exhausted{false};

            RenderThreadPool::Get().RunInArena([&]
                                               {
                tbb::parallel_for(
                    tbb::blocked_range<uint32_t>(0, nWorkers, 1),
                    [&](const tbb::blocked_range<uint32_t> &) {
                        WorkerState &ws = ets.local();
                        if (!ws.iws)
                            ws.iws = integrator.NewWorkerState();
                        PixelClaim c;
                        while (m_state.load(std::memory_order_relaxed) ==
                               RenderState::Run)
                        {
                            if (haltTime > 0 &&
                                std::chrono::duration<double>(
                                    std::chrono::steady_clock::now() - startTime)
                                        .count() >= double(haltTime))
                            {
                                exhausted.store(true, std::memory_order_relaxed);
                                return;
                            }
                            if (!ClaimNext(cursor, totalPixels, c))
                            {
                                exhausted.store(true, std::memory_order_relaxed);
                                return;
                            }
                            int x, y;
                            ws.pixelSampler->GetPixel(c.pos, fullTile, &x, &y);
                            const Tile onePx{x, y, x + 1, y + 1};
                            integrator.RenderTile(scene, onePx, film,
                                                  *ws.sampler, ws.iws.get(),
                                                  sampleBasis, batch);
                            film.AddSampleCount(double(batch));
                        }
                    },
                    tbb::simple_partitioner()); });

            // All worker loops returned for this pass.
            if (m_state.load() == RenderState::Terminate)
                break;
            if (exhausted.load(std::memory_order_relaxed))
                ++pass;

            // Pause blocks until Resume, then reenters the same pass.
            while (m_state.load() == RenderState::Pause)
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
        } // pass / pause loop

        integrator.End(scene);
    }

    std::shared_ptr<Renderer> SamplerRenderer::CreateRenderer(
        const PluginContext &ctx)
    {
        (void)ctx;
        return std::make_shared<SamplerRenderer>();
    }

    LUX2_REGISTER_RENDERER(SamplerRenderer, "sampler");

} // namespace lux2
