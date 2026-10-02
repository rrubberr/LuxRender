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
#include <tbb/parallel_for_each.h>
#include <tbb/task_group.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <thread>
#include <type_traits>
#include <vector>

namespace lux2
{

    namespace
    {
        // Per-worker state: a sampler clone plus one scratch buffer allocated
        // at the largest tile window and retargeted per item via BindScratch.
        // Lives in an enumerable_thread_specific slot, so it persists across
        // parallel_for_each re-entries (pause/resume).
        struct WorkerState
        {
            std::unique_ptr<Sampler> sampler;
            std::unique_ptr<Film> scratch;
        };

        // --- SplatSink seam -------------------------------------------------
        // The write policy is selected at COMPILE time by a tag, never by a
        // virtual call inside the integrator's Enoki splat loop. RenderTile
        // always writes into a Film&; the sink decides how that Film reaches
        // the shared frame and how samples are accounted. This keeps the hot
        // path free of dispatch while letting correlated samplers drop in
        // later without touching the scheduler loop.
        struct UncorrelatedSink
        {
        }; // scratch tile -> MergeScratch once per visit

        struct CorrelatedSink
        {
        }; // atomic splat to shared frame (MLT) — not implemented yet

        // Render one work item through the given sink and commit it. The tag is
        // a template parameter, so `if constexpr` selects exactly one branch per
        // instantiation — no runtime dispatch, and the hot splat loop inside
        // RenderTile is untouched.
        template <class Sink>
        void RunItem(const Sink &, const Scene &scene,
                     SurfaceIntegrator &integrator, Film &film,
                     WorkerState &ws, uint32_t tile,
                     uint32_t count, std::atomic<uint32_t> &cursor)
        {
            if constexpr (std::is_same_v<Sink, UncorrelatedSink>)
            {
                // The tile index fully determines the scratch window and merge
                // targets. Allocate the worker's scratch once at max size, then
                // retarget it to this tile (which also zeroes it). No coordinate
                // is passed by the caller, so a mismatch cannot be expressed.
                const FilmTile ft = film.GetTile(tile);
                const Tile tileRect{ft.x0, ft.y0, ft.x1, ft.y1};

                if (!ws.scratch)
                {
                    ws.scratch = film.MakeWorkerScratch();
                    if (!ws.scratch)
                        return;
                }
                film.BindScratch(tile, *ws.scratch);

                // Disjoint sample range per visit -> unique seed basis.
                const uint64_t begin =
                    cursor.fetch_add(count, std::memory_order_relaxed);
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
        // 2 x nWorkers tiles (legacy-safe default), clamped by filter halo and
        // a 128px display-granularity cap.
        // Filter halo: how far a splat's footprint reaches past its center. The
        // scratch window and overlap directory are grown by this so boundary
        // splats merge into neighbors instead of clipping.
        const int haloX = int(std::ceil(filter.GetXWidth()));
        const int haloY = int(std::ceil(filter.GetYWidth()));
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

        // Per-tile monotonic sample cursor (seed basis source) and in-flight
        // accounting for the global lowest-count refill policy.
        // make_unique value-initializes the atomics to 0 and avoids the
        // non-copyable vector<atomic> constructor.
        auto tileCursor = std::make_unique<std::vector<std::atomic<uint32_t>>>(
            size_t(tileCount));
        auto inFlight = std::make_unique<std::vector<std::atomic<uint32_t>>>(
            size_t(tileCount));

        const int haltSpp = film.HaltSpp();
        const int haltTime = film.HaltTime();
        const auto startTime = std::chrono::steady_clock::now();

        uint32_t tileSpp = m_tileSpp;
        if (tileSpp == 0)
            tileSpp = 16; // conservative default until cost-based tuning lands

        TileState tileState;
        tileState.committed = tileCursor.get();
        tileState.inFlight = inFlight.get();
        tileState.tileCount = tileCount;
        tileState.baseCount = tileSpp;
        tileState.haltSpp = haltSpp;

        // Pluggable backfill policy. Uniform ships first; guided (cost-weighted
        // redistribution) can replace it without touching the scheduler loop.
        UniformBackfill<tbb::feeder<WorkItem>> backfill;

        integrator.Start(scene);

        // ETS constructs each worker slot by cloning the prototype sampler;
        // WorkerState holds unique_ptrs so it cannot be copy-constructed.
        const Sampler *protoSamplerPtr = &protoSampler;
        auto makeWorker = [protoSamplerPtr] {
            WorkerState ws;
            ws.sampler = protoSamplerPtr->Clone();
            return ws;
        };

        // Each RunInArena call is one continuous parallel_for_each. Pause drains
        // the current region (stop feeding), then I block until Resume and start
        // a fresh region; per-tile cursors/counters persist, so nothing is lost.
        while (true)
        {
        if (m_state.load() == RenderState::Terminate)
            break;

        tbb::task_group_context tgc; // Terminate cancels promptly
        RenderThreadPool::Get().RunInArena([&] {
            tbb::enumerable_thread_specific<WorkerState> ets(makeWorker);

            // Seed items per tile so stealing has something to grab; each
            // completion feeds one more under the global policy. Clamp the seed
            // batch to haltSpp so a small halt isn't blown past by the default
            // batch, and drop the second seed pass when two batches would
            // overshoot haltSpp (backfill tops up to exactly haltSpp after).
            auto &inFlightV = *inFlight;
            auto &cursorV = *tileCursor;
            const uint32_t seedCount =
                (haltSpp > 0) ? std::min(tileSpp, uint32_t(haltSpp)) : tileSpp;
            const int seedPasses =
                (haltSpp > 0 && 2ull * seedCount > uint64_t(haltSpp)) ? 1 : 2;
            std::vector<WorkItem> seed;
            seed.reserve(size_t(tileCount) * size_t(seedPasses));
            for (int pass = 0; pass < seedPasses; ++pass)
                for (int t = 0; t < tileCount; ++t)
                {
                    inFlightV[t].fetch_add(1, std::memory_order_relaxed);
                    seed.push_back(WorkItem{uint32_t(t), seedCount});
                }

            tbb::parallel_for_each(
                seed.begin(), seed.end(),
                [&](const WorkItem &it, tbb::feeder<WorkItem> &feeder) {
                    WorkerState &ws = ets.local();
                    RunItem(UncorrelatedSink{}, scene, integrator, film,
                            ws, it.tile, it.count, cursorV[it.tile]);
                    inFlightV[it.tile].fetch_sub(1, std::memory_order_relaxed);

                    if (ShouldStop(film, haltSpp, haltTime, startTime, tgc))
                        return; // halt trips: cancel + stop feeding
                    backfill.Refill(feeder, tileState);
                },
                tgc);
        });

        // Region drained. Terminate or a halt ends rendering; Pause blocks
        // until Resume, then re-enters with the same cursors/counters.
        if (m_state.load() == RenderState::Terminate)
            break;
        if (haltSpp > 0 && film.SampleCount() >= double(haltSpp))
            break;
        if (haltTime > 0 &&
            std::chrono::duration<double>(
                std::chrono::steady_clock::now() - startTime)
                    .count() >= double(haltTime))
            break;
        while (m_state.load() == RenderState::Pause)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        } // pause/resume loop

        // Final top-up: with min-across-tiles halt, a hotspot can trip the halt
        // while cheap tiles lag behind. Bring every tile up to haltspp (bounded),
        // so the reported SPP is uniform rather than pinned by the slowest tile.
        if (haltSpp > 0 && m_state.load() != RenderState::Terminate)
        {
            auto &cursorV = *tileCursor;
            tbb::task_group_context topupCtx;
            RenderThreadPool::Get().RunInArena([&] {
                tbb::enumerable_thread_specific<WorkerState> ets(makeWorker);
                std::vector<WorkItem> pending;
                for (int t = 0; t < tileCount; ++t)
                {
                    const uint32_t have =
                        cursorV[t].load(std::memory_order_relaxed);
                    if (int(have) < haltSpp)
                        pending.push_back(
                            WorkItem{uint32_t(t), uint32_t(haltSpp - int(have))});
                }
                tbb::parallel_for_each(
                    pending.begin(), pending.end(),
                    [&](const WorkItem &it, tbb::feeder<WorkItem> &) {
                        WorkerState &ws = ets.local();
                        RunItem(UncorrelatedSink{}, scene, integrator, film,
                                ws, it.tile, it.count,
                                cursorV[it.tile]);
                    },
                    topupCtx);
            });
        }

        integrator.End(scene);
    }

    bool SamplerRenderer::ShouldStop(Film &film, int haltSpp, int haltTime,
                                     std::chrono::steady_clock::time_point start,
        tbb::task_group_context &tgc) const
    {
        bool stop = false;
        if (m_state.load() == RenderState::Terminate)
            stop = true;
        if (!stop && haltTime > 0)
        {
            const double elapsed =
                std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - start)
                    .count();
            if (elapsed >= double(haltTime))
                stop = true;
        }
        if (!stop && haltSpp > 0 && film.SampleCount() >= double(haltSpp))
            stop = true;
        // Cancel so the region stops scheduling new items promptly rather than
        // draining a large backlog past the halt. In-flight items finish.
        if (stop)
            tgc.cancel_group_execution();
        return stop;
    }

    std::shared_ptr<Renderer> SamplerRenderer::CreateRenderer(
        const PluginContext &ctx)
    {
        ParamSet *p = ctx.params;
        const int tileSize = p ? p->FindOneInt("tilesize", 0) : 0;
        // Scene keys "tilespp" (and legacy "passspp") are external config and
        // stay as-is; only the C++ symbols follow the haltSpp casing.
        int tileSpp = p ? p->FindOneInt("tilespp", 0) : 0;
        if (tileSpp == 0 && p)
            tileSpp = p->FindOneInt("passspp", 0);
        return std::make_shared<SamplerRenderer>(
            tileSize, uint32_t(std::max(0, tileSpp)));
    }

    LUX2_REGISTER_RENDERER(SamplerRenderer, "sampler");

} // namespace lux2
