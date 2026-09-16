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

// Abstract surface and volume integrator.

#include "core/vecp.h"
#include "core/spectrum.h"
#include "core/ray.h"
#include "core/sampler.h"

namespace lux2 {

class Scene;        // Defined in core/scene.h.
class TileQueue;    // Defined in core/tilequeue.h.

// =======================================================================
// SurfaceIntegrator
// =======================================================================

// Abstract surface integrator. Solves the rendering equation over a
// stream of tiles. The interface is multi pass so can be reused for photon mapping.
class SurfaceIntegrator {
public:
    virtual ~SurfaceIntegrator() = default;

    // Number of rendering passes this integrator requires. Default 1.
    virtual int PassCount() const { return 1; }

    // Called once before the first pass.
    virtual void Start(const Scene& scene) { (void)scene; }

    // Execute one rendering pass over the tiles.
    virtual void RenderPass(const Scene& scene, TileQueue& tiles,
                            Sampler& sampler, int passIndex) = 0;

    // Called once after the last pass. Flush pass buffers.
    virtual void End(const Scene& scene) { (void)scene; }
};

// =======================================================================
// VolumeIntegrator
// =======================================================================

// Abstract volume integrator.
class VolumeIntegrator {
public:
    virtual ~VolumeIntegrator() = default;

    // Radiance accumulated along the ray segment.
    virtual SWCSpectrumP Li(const RayP& ray, MaskP active = MaskP(true)) const {
        (void)ray; (void)active;
        return SWCSpectrumP(0.f);
    }
};

} // namespace lux2

#endif // LUX2_INTEGRATOR_H
