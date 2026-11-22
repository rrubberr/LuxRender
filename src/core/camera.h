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

#include "core/math/vecp.h"
#include "core/math/geometry.h"
#include "core/math/ray.h"

namespace lux2
{

    class EmbreeScene;

    // Generates a packet of primary rays for a set of sample positions on the film.
    class Camera
    {
    public:
        virtual ~Camera() = default;

        // Extra per-ray random streams beyond pixel jitter (0 = pinhole,
        // 2 = depth of field). Integrators draw this many UNIFORM [0,1) dims
        // and pass them to GenerateRay/SampleRay as uLensX/uLensY; the camera
        // maps them onto the aperture itself, so integrators stay ignorant of
        // the aperture shape.
        virtual int SampleDimensionCount() const { return 0; }

        // Fill `ray` for the given film coordinates. uLensX/uLensY are raw
        // uniforms consumed only when SampleDimensionCount() >= 2.
        virtual void GenerateRay(const FloatP &x, const FloatP &y,
                                 const FloatP &time, RayP *ray,
                                 FloatP *weight,
                                 MaskP active = MaskP(true),
                                 const FloatP &uLensX = FloatP(0.f),
                                 const FloatP &uLensY = FloatP(0.f)) const = 0;

        // Returns the ray, its sampling PDF, and emission weight.
        virtual void SampleRay(const FloatP &uPixelX, const FloatP &uPixelY,
                               const FloatP &time, RayP *ray, FloatP *pdf,
                               FloatP *weight,
                               MaskP active = MaskP(true),
                               const FloatP &uLensX = FloatP(0.f),
                               const FloatP &uLensY = FloatP(0.f)) const
        {
            const FloatP x = uPixelX * FloatP(PixelWidth());
            const FloatP y = uPixelY * FloatP(PixelHeight());
            GenerateRay(x, y, time, ray, weight, active, uLensX, uLensY);
            enoki::masked(*pdf, active) = Pdf(*ray, active);
        }

        // PDF of generating a given ray for bidirectional/MLT.
        virtual FloatP Pdf(const RayP &ray, MaskP active = MaskP(true)) const
        {
            return FloatP(0.f);
        }

        // Inverse of GenerateRay.
        virtual MaskP Project(const Vector3fP & /*dirWorld*/, FloatP * /*x*/,
                              FloatP * /*y*/, FloatP * /*We*/,
                              FloatP * /*cosCam*/, MaskP /*active*/,
                              const Point3fP & /*origin*/) const
        {
            return MaskP(false);
        }

        // Film resolution.
        virtual int PixelWidth() const = 0;
        virtual int PixelHeight() const = 0;

        // Worldspace origin when the camera sits at a point.
        virtual bool OriginPoint(Point3f * /*out*/) const { return false; }

        // Resolve focus distance from the scene after geometry is committed.
        virtual void AutoFocus(const EmbreeScene *) {}
    };

} // namespace lux2

#endif // LUX2_CAMERA_H
