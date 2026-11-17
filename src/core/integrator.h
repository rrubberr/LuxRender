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

#ifndef LUX2_INTEGRATOR_H
#define LUX2_INTEGRATOR_H

#include "core/math/vecp.h"
#include "core/color/spectrum.h"
#include "core/math/ray.h"
#include "core/sampler.h"
#include "core/tilequeue.h"

#include <memory>

namespace lux2
{

    class Scene;
    class Film;

    // ---------------------------------------------------------------------------
    // IntegratorWorkerState
    // ---------------------------------------------------------------------------

    struct IntegratorWorkerState
    {
        virtual ~IntegratorWorkerState() = default;
    };

    // ---------------------------------------------------------------------------
    // SurfaceIntegrator
    // ---------------------------------------------------------------------------

    // Abstract surface integrator.
    class SurfaceIntegrator
    {
    public:
        virtual ~SurfaceIntegrator() = default;

        // Number of rendering passes this integrator requires. Default 1.
        virtual int PassCount() const { return 1; }

        // Called once before the first pass.
        virtual void Start(const Scene &scene) { (void)scene; }

        // True if this integrator advances rays through media.
        virtual bool SupportsMedia() const { return false; }

        // Build this worker's persistent state.
        virtual std::unique_ptr<IntegratorWorkerState> NewWorkerState() const
        {
            return nullptr;
        }

        // Render spp samples per pixel of tile into dest. sampleBasis
        // is a cursor used to decorrelate repeat visits to the pixel. ws is
        // this worker's persistent state from NewWorkerState() (may be null).
        virtual void RenderTile(const Scene &scene, const Tile &tile,
                                Film &dest, Sampler &sampler,
                                IntegratorWorkerState *ws,
                                uint64_t sampleBasis, uint32_t spp) = 0;

        // Called once after the last pass. Flush pass buffers.
        virtual void End(const Scene &scene) { (void)scene; }
    };

    // ---------------------------------------------------------------------------
    // VolumeIntegrator
    // ---------------------------------------------------------------------------

    // Abstract volume integrator.
    class VolumeIntegrator
    {
    public:
        virtual ~VolumeIntegrator() = default;

        // Radiance accumulated along the ray segment.
        virtual SWCSpectrumP Li(const RayP &ray, MaskP active = MaskP(true)) const
        {
            (void)ray;
            (void)active;
            return SWCSpectrumP(0.f);
        }
    };

} // namespace lux2

#endif // LUX2_INTEGRATOR_H
