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

#ifndef LUX2_CAMERA_H
#define LUX2_CAMERA_H

#include "core/vecp.h"
#include "core/geometry.h"
#include "core/ray.h"

namespace lux2
{

    // Generates a packet of primary rays for a set of sample positions on the film.
    class Camera
    {
    public:
        virtual ~Camera() = default;

        // Fill `ray` for the given film coordinates.
        virtual void GenerateRay(const FloatP &x, const FloatP &y,
                                 const FloatP &time, RayP *ray,
                                 FloatP *weight,
                                 MaskP active = MaskP(true)) const = 0;

        // Returns the ray, its sampling PDF, and emission weight.
        virtual void SampleRay(const FloatP &uPixelX, const FloatP &uPixelY,
                               const FloatP &time, RayP *ray, FloatP *pdf,
                               FloatP *weight,
                               MaskP active = MaskP(true)) const
        {
            const FloatP x = uPixelX * FloatP(PixelWidth());
            const FloatP y = uPixelY * FloatP(PixelHeight());
            GenerateRay(x, y, time, ray, weight, active);
            enoki::masked(*pdf, active) = Pdf(*ray, active);
        }

        // PDF of generating a given ray for bidirectional/MLT.
        virtual FloatP Pdf(const RayP &ray, MaskP active = MaskP(true)) const
        {
            return FloatP(0.f);
        }

        // Film resolution.
        virtual int PixelWidth() const = 0;
        virtual int PixelHeight() const = 0;
    };

} // namespace lux2

#endif // LUX2_CAMERA_H
