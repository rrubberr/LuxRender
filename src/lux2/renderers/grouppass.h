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

#ifndef LUX2_RENDERERS_GROUPPASS_H
#define LUX2_RENDERERS_GROUPPASS_H

#include <atomic>
#include <cstdint>

namespace lux2
{

    class Renderer;
    class Scene;
    class SurfaceIntegrator;

    // Ensure a render is reproducible for a given base seed.
    uint64_t SplitMix64(uint64_t x);

    // Distinct sampler base seed per lightgroup.
    uint64_t GroupPassSeed(uint64_t baseSeed, int round, int group);

    // Lightgroup render wrapper.
    void RunGroupPasses(Renderer &renderer, Scene &scene,
                        SurfaceIntegrator &integrator,
                        const std::atomic<bool> *aborted = nullptr);

} // namespace lux2

#endif // LUX2_RENDERERS_GROUPPASS_H
