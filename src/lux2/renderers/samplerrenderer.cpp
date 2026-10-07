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
#include "core/filter.h"
#include "core/integrator.h"
#include "core/register.h"
#include "core/sampler.h"
#include "core/schedulerpolicy.h"
#include "core/scene.h"
#include "core/threadpool.h"

#include <tbb/enumerable_thread_specific.h>
#include <tbb/parallel_for.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>
#include <type_traits>
#include <vector>

namespace lux2
{

    namespace
    {
        // Worker state holds a sampler clone plus one scratch buffer allocated
        // at the largest tile window and retargeted per item via BindScratch.
        struct WorkerState
        {
            std::unique_ptr<Sampler> sampler;
            std::unique_ptr<Film> scratch;
        };

        // The write policy is selected by COMPILE time for now.
        struct UncorrelatedSink
        {
        }; // scratch tile -> MergeScratch once per visit

        struct CorrelatedSink
        {
        }; // atomic splat to shared frame (MLT)

        // Render one chunk and commit it.
        template <class Sink>
        void RunItem(const Sink &, const Scene &scene,
                     SurfaceIntegrator &integrator, Film &film,
                     WorkerState &ws, uint32_t tile,
                     uint64_t begin, uint32_t count)
        {
            if constexpr (std::is_same_v<Sink, UncorrelatedSink>)
            {
                // The tile index determines the scratch window and merge
                // targets. Allocate the worker's scratch once at max size.
                const FilmTile ft = film.GetTile(tile);
                const Tile tileRect{ft.x0, ft.y0, ft.x1, ft.y1};

                if (!ws.scratch)
                {
                    ws.scratch = film.MakeWorkerScratch();
                    if (!ws.scratch)
                        return;
                }
                film.BindScratch(tile, *ws.scratch);

                // Disjoint sample range gives a unique seed basis.
                integrator.RenderTile(scene, tileRect, *ws.scratch, *ws.sampler,
                                      begin, count);
                film.MergeScratch(tile, *ws.scratch); // locks only here
                film.AddTileSampleCount(tile, double(count));
            }
            else
            {
                // Correlated (MLT): TODO. A chain lives in the worker slot;
                // writes go directly to the shared frame via atomic scatter_add
                // (low splat rate, scattered addresses), with a batched splat-log
                // as a fallback if profiling demands it. Accounting switches to a
                // global mutation counter and the film applies a bootstrap scale
                // at snapshot time. Intentionally empty until the MLT sink lands.
            }
        }
    } // namespace

    void SamplerRenderer::Render(const Scene &scene, SurfaceIntegrator &integrator)
    {
        m_state.store(RenderState::Run);

        Film &film = scene.GetFilm();
        const Sampler &protoSampler = scene.GetSampler();
        const Filter &filter = scene.GetFilter();

        RenderThreadPool &pool = RenderThreadPool::Get();
        if (pool.Count() == 0)
            pool.Init(0); // default: hardware_concurrency
        const uint32_t nWorkers = std::max(1u, pool.Count());

        if (protoSampler.IsCorrelated())
            LOG(LUX_WARNING, LUX_NOERROR)
                << "SamplerRenderer: correlated sampler in use; the atomic-splat "
                   "sink is not implemented yet. Falling back to tiled scratch.";

        // Partition the film into a tile grid. Auto tile size targets
        // 2 * nWorkers tiles, clamped by filter halo and 128pxy.
        // Halo is how far a splat's footprint reaches past its center.
        const int haloX = enoki::ceil2int<int>(filter.GetXWidth());
        const int haloY = enoki::ceil2int<int>(filter.GetYWidth());
        int tileSize = m_tileSize;
        if (tileSize <= 0)
        {
            const double frameArea =
                double(film.XCount()) * double(film.YCount());
            const double targetTiles = double(2 * nWorkers);
            const int minEdge =
                std::max(int(PACKET_WIDTH),
                         2 * std::max(haloX, haloY) + 1);
            const int ideal =
                int(std::sqrt(std::max(1.0, frameArea / targetTiles)));
            tileSize = std::min(128, std::max(minEdge, ideal));
        }
        const int tileCount = film.SetupTiles(tileSize, haloX, haloY);
        if (tileCount <= 0)
            return;

        // Tile sample cursor.
        auto tileCursor = std::make_unique<std::vector<std::atomic<uint32_t>>>(
            size_t(tileCount));

        const int haltSpp = film.HaltSpp();
        const int haltTime = film.HaltTime();
        const auto startTime = std::chrono::steady_clock::now();

        uint32_t tileSpp = m_tileSpp;
        if (tileSpp == 0)
            tileSpp = 32; // BindScratch/MergeScratch cost per visit

        integrator.Start(scene);

        // Construct each worker slot by cloning the sampler.
        // Outside pause/resume so the scratch and sampler survive pause.
        const Sampler *protoSamplerPtr = &protoSampler;
        auto makeWorker = [protoSamplerPtr]
        {
            WorkerState ws;
            ws.sampler = protoSamplerPtr->Clone();
            return ws;
        };
        tbb::enumerable_thread_specific<WorkerState> ets(makeWorker);

        // Each worker runs a loop taking chunks from the least sampled
        // tile until every tile reaches haltSpp.
        auto &cursorV = *tileCursor;
        std::atomic<bool> finished{false};
        while (true)
        {
            if (m_state.load() == RenderState::Terminate)
                break;

            RenderThreadPool::Get().RunInArena([&]
                                               {
                // One loop per worker.
                tbb::parallel_for(
                    tbb::blocked_range<uint32_t>(0, nWorkers, 1),
                    [&](const tbb::blocked_range<uint32_t> &) {
                        WorkerState &ws = ets.local();
                        const uint32_t hint = uint32_t(
                            std::hash<std::thread::id>{}(std::this_thread::get_id()));
                        Claim c;
                        while (m_state.load(std::memory_order_relaxed) ==
                               RenderState::Run)
                        {
                            if (haltTime > 0 &&
                                std::chrono::duration<double>(
                                    std::chrono::steady_clock::now() - startTime)
                                        .count() >= double(haltTime))
                            {
                                finished.store(true, std::memory_order_relaxed);
                                return;
                            }
                            if (!ClaimNext(cursorV, haltSpp, tileSpp, hint, c))
                            {
                                finished.store(true, std::memory_order_relaxed);
                                return;
                            }
                            RunItem(UncorrelatedSink{}, scene, integrator, film,
                                    ws, c.tile, c.begin, c.count);
                        }
                    },
                    tbb::simple_partitioner()); });

            // All loops returned. Terminate or complete here.
            // Pause blocks until Resume and reenters with the same cursors.
            if (finished.load(std::memory_order_relaxed) ||
                m_state.load() == RenderState::Terminate)
                break;
            while (m_state.load() == RenderState::Pause)
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
        } // pause/resume loop

        integrator.End(scene);
    }

    std::shared_ptr<Renderer> SamplerRenderer::CreateRenderer(
        const PluginContext &ctx)
    {
        ParamSet *p = ctx.params;
        const int tileSize = p ? p->FindOneInt("tilesize", 0) : 0;
        int tileSpp = p ? p->FindOneInt("tilespp", 0) : 0;
        return std::make_shared<SamplerRenderer>(
            tileSize, uint32_t(std::max(0, tileSpp)));
    }

    LUX2_REGISTER_RENDERER(SamplerRenderer, "sampler");

} // namespace lux2
