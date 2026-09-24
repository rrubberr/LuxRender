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

#ifndef LUX2_RAY_H
#define LUX2_RAY_H

#include "core/vecp.h"
#include "core/geometry.h"
#include "core/spectrum.h"

#include <limits>

namespace lux2
{

    // ---------------------------------------------------------------------------
    // RayP
    // ---------------------------------------------------------------------------

    struct RayP
    {
        Point3fP o;         // ray origin (3*FloatP)
        Vector3fP d;        // ray direction (3*FloatP)
        Vector3fP d_rcp;    // component-wise reciprocal of d
        FloatP mint;        // minimum distance on the ray segment
        FloatP maxt;        // maximum distance on the ray segment
        FloatP time;        // motion blur time
        FloatP wavelengths; // SWA wavelengths

        // Visibility-group bitmask.
        UInt32P mask;

        // Integrator payload
        SWCSpectrumP throughput; // accumulated spectral throughput
        FloatP pdf;              // PDF of the current direction
        Int32P depth;            // bounce count
        MaskP specularBounce;    // previous vertex was a specular interaction
        MaskP alive;             // lane activity mask

        RayP() = default;

        // Construct a ray packet from origin/direction.
        RayP(const Point3fP &o, const Vector3fP &d,
             const FloatP &mint, const FloatP &maxt, const FloatP &time)
            : o(o), d(d), d_rcp(enoki::rcp(d)), mint(mint), maxt(maxt), time(time) {}

        // Recompute d_rcp after mutating d.
        void UpdateReciprocalDirection() { d_rcp = enoki::rcp(d); }

        // Position along the ray at parameter t: o + d * t.
        Point3fP operator()(const FloatP &t) const { return fmadd(d, t, o); }

        // Initialize the payload for a primary ray batch.
        void InitPayload()
        {
            throughput = SWCSpectrumP(1.f);
            pdf = FloatP(0.f);
            depth = Int32P(0);
            specularBounce = MaskP(false);
            alive = MaskP(true);
            mask = UInt32P(0xFFFFFFFFu);
        }
    };

    // ---------------------------------------------------------------------------
    // HitP intersection result (Embree hit -> shading lookup)
    // ---------------------------------------------------------------------------

    // The result of tracing a RayP packet against the Embree scene.
    struct HitP
    {
        UInt32P geomID; // mesh/geometry index
        UInt32P primID; // triangle index within the geometry
        FloatP t;       // hit distance along the ray
        FloatP b1, b2;  // barycentrics; b0 = 1 - b1 - b2
        MaskP hit;      // active && tfar changed

        // Derived by ShadeHit().
        Point3fP p;     // interpolated position
        Normal3fP ngeo; // face-forward geometric normal
        Normal3fP sh_n; // normalized interpolated vertex normal
        Point2fP uv;    // interpolated texture coords
        UInt32P matID;  // per-triangle material index
        Int32P lightID; // per-triangle area-light index, or -1
    };

} // namespace lux2

#endif // LUX2_RAY_H
