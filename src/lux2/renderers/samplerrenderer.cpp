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

#include "core/dynload.h"
#include "core/error.h"
#include "core/film.h"
#include "core/filter.h"
#include "core/register.h"
#include "core/scene.h"
#include "core/threadpool.h"
#include "core/tilequeue.h"

#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

namespace lux2
{

    namespace
    {
        // Drain a TileQueue into an indexable vector.
        std::vector<Tile> CollectTiles(TileQueue *q)
        {
            std::vector<Tile> tiles;
            Tile t;
            while (q->Next(&t))
                tiles.push_back(t);
            return tiles;
        }
    } // namespace

    void SamplerRenderer::Render(const Scene &scene, SurfaceIntegrator &integrator)
    {
        m_state.store(RenderState::Run);

        Film &film = scene.GetFilm();
        const Sampler &protoSampler = scene.GetSampler();
        const Filter &filter = scene.GetFilter();

        // Crop window bounds.
        const int x0 = film.XStart(), y0 = film.YStart();
        const int x1 = x0 + film.XCount(), y1 = y0 + film.YCount();

        // Worker sampler slots keyed by arena thread index.
        RenderThreadPool &pool = RenderThreadPool::Get();
        if (pool.Count() == 0)
            pool.Init(0); // default: hardware_concurrency
        std::vector<std::unique_ptr<Sampler>> slots(pool.Count());

        // Tiles are rendered into blocks expanded by the filter radius
        // so splats crossing a tile edge are not clipped.
        const int fx = int(std::ceil(filter.GetXWidth()));
        const int fy = int(std::ceil(filter.GetYWidth()));

        TileQueue queue;
        BuildTiles(x0, y0, x1 - x0, y1 - y0, m_tileSize, &queue);
        const std::vector<Tile> tiles = CollectTiles(&queue);
        if (tiles.empty())
            return;

        if (protoSampler.IsCorrelated())
            LOG(LUX_WARNING, LUX_NOERROR)
                << "SamplerRenderer: correlated sampler in use; the pinned-region "
                   "scheduler is not implemented yet. Falling back to tiled.";

        const uint32_t target = protoSampler.SampleCount();
        const int haltSpp = film.HaltSpp();
        const int haltTime = film.HaltTime();

        integrator.Start(scene);

        const auto startTime = std::chrono::steady_clock::now();
        int passIndex = 0;

        while (true)
        {
            // Pause at pass boundaries.
            RenderState st = m_state.load();
            if (st == RenderState::Terminate)
                break;
            if (st == RenderState::Pause)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }

            const double accumulated = film.SampleCount();
            if (accumulated >= double(target))
                break;
            if (haltSpp > 0 && accumulated >= double(haltSpp))
                break;
            if (haltTime > 0)
            {
                const double elapsed =
                    std::chrono::duration<double>(
                        std::chrono::steady_clock::now() - startTime)
                        .count();
                if (elapsed >= double(haltTime))
                    break;
            }

            // Budget: never render past the spp target or the haltspp target.
            double budget = std::min(double(m_passSpp), double(target) - accumulated);
            if (haltSpp > 0)
                budget = std::min(budget, double(haltSpp) - accumulated);
            if (budget <= 0.0)
                break;
            const uint32_t sppThisPass =
                std::max(1u, uint32_t(std::floor(budget)));

            RenderThreadPool::Get().RunInArena([&]
                                               { tbb::parallel_for(
                                                     tbb::blocked_range<size_t>(0, tiles.size(), 1),
                                                     [&](const tbb::blocked_range<size_t> &r)
                                                     {
                                                         const int idx =
                                                             tbb::this_task_arena::current_thread_index();
                                                         std::unique_ptr<Sampler> &sampler = slots[size_t(idx)];
                                                         if (!sampler)
                                                             sampler = protoSampler.Clone();

                                                         for (size_t i = r.begin(); i != r.end(); ++i)
                                                         {
                                                             if (m_state.load() == RenderState::Terminate)
                                                                 return; // cooperative cancellation per tile

                                                             const Tile &tile = tiles[i];
                                                             const int ex0 = std::max(tile.x0 - fx, x0);
                                                             const int ey0 = std::max(tile.y0 - fy, y0);
                                                             const int ex1 = std::min(tile.x1 + fx, x1);
                                                             const int ey1 = std::min(tile.y1 + fy, y1);

                                                             std::unique_ptr<Film> block =
                                                                 film.MakePrivateBlock(ex0, ey0, ex1, ey1);
                                                             if (!block)
                                                                 continue;

                                                             integrator.RenderTile(scene, tile, *block,
                                                                                   *sampler, passIndex,
                                                                                   sppThisPass);

                                                             film.MergeRegion(block.get(), ex0, ey0, ex1, ey1);
                                                         }
                                                     }); });

            // SPP accounting is per pass, not per tile.
            film.AddSampleCount(double(sppThisPass));
            ++passIndex;
        }

        integrator.End(scene);
    }

    std::shared_ptr<Renderer> SamplerRenderer::CreateRenderer(
        const PluginContext &ctx)
    {
        ParamSet *p = ctx.params;
        const int tileSize = p ? p->FindOneInt("tilesize", 64) : 64;
        const int passSpp = p ? p->FindOneInt("passspp", 32) : 32;
        return std::make_shared<SamplerRenderer>(
            tileSize, uint32_t(std::max(1, passSpp)));
    }

    LUX2_REGISTER_RENDERER(SamplerRenderer, "sampler");

} // namespace lux2
