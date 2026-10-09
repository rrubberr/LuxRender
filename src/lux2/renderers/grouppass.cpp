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

#include "renderers/grouppass.h"

#include "core/film.h"
#include "core/renderer.h"
#include "core/sampler.h"
#include "core/scene.h"

namespace lux2
{

    uint64_t SplitMix64(uint64_t x)
    {
        x += 0x9E3779B97F4A7C15ull;
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
        return x ^ (x >> 31);
    }

    uint64_t GroupPassSeed(uint64_t baseSeed, int round, int group)
    {
        const uint64_t rg =
            (uint64_t(uint32_t(round)) << 32) | uint32_t(group);
        return SplitMix64(baseSeed ^ rg);
    }

    void RunGroupPasses(Renderer &renderer, Scene &scene,
                        SurfaceIntegrator &integrator,
                        const std::atomic<bool> *aborted)
    {
        Film &film = scene.GetFilm();
        const int groupCount = scene.LightGroupCount();

        if (groupCount <= 1 && film.GroupConvertIsIdentity(0))
        {
            // Default lightgroup.
            renderer.Render(scene, integrator);
            return;
        }

        class Sampler &sampler = scene.GetSampler();
        const uint64_t baseSeed = sampler.BaseSeed();
        for (int g = 0; g < groupCount; ++g)
        {
            if (aborted && aborted->load())
                break; // abort skips remaining passes
            if (!scene.GroupHasLights(g))
                continue; // D10: empty group, no AOV
            // ActiveGroup is set between passes.
            scene.SetActiveGroup(g);
            film.SetActiveGroup(g);
            sampler.SetBaseSeed(GroupPassSeed(baseSeed, 0, g));
            renderer.Render(scene, integrator);
        }
        // Restore.
        scene.SetActiveGroup(-1);
        film.SetActiveGroup(-1);
    }

} // namespace lux2
