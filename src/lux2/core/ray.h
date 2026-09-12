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

// RayP packet for the path integrator.

#include "core/vecp.h"
#include "core/geometry.h"
#include "core/spectrum.h"

#include <limits>

namespace lux2 {

// =======================================================================
// RayP
// =======================================================================

struct RayP {
    Point3fP o;         ///< Ray origin (3 x FloatP)
    Vector3fP d;        ///< Ray direction (3 x FloatP)
    Vector3fP d_rcp;    ///< Component-wise reciprocal of d (for slab tests)
    FloatP mint;        ///< Minimum distance on the ray segment
    FloatP maxt;        ///< Maximum distance on the ray segment
    FloatP time;        ///< Motion-blur time
    FloatP wavelengths; ///< SWA wavelengths (4 samples handled by SWCSpectrumP)

    // Integrator payload
    SWCSpectrumP throughput; ///< Accumulated spectral throughput
    FloatP pdf;              ///< PDF of the current direction
    Int32P depth;            ///< Bounce count
    MaskP specularBounce;    ///< Previous vertex was a specular interaction
    MaskP alive;             ///< Lane activity mask

    RayP() = default;

    /// Construct a ray packet from SoA origin/direction, computing d_rcp.
    RayP(const Point3fP &o, const Vector3fP &d,
         const FloatP &mint, const FloatP &maxt, const FloatP &time)
        : o(o), d(d), d_rcp(enoki::rcp(d)), mint(mint), maxt(maxt), time(time) { }

    /// Recompute d_rcp after mutating d.
    void UpdateReciprocalDirection() { d_rcp = enoki::rcp(d); }

    /// Position along the ray at parameter t: o + d * t.
    Point3fP operator()(const FloatP &t) const {
        return Point3fP(fmadd(d.x(), t, o.x()),
                        fmadd(d.y(), t, o.y()),
                        fmadd(d.z(), t, o.z()));
    }

    /// Initialize the payload for a fresh primary-ray batch.
    void InitPayload() {
        throughput = SWCSpectrumP(1.f);
        pdf = FloatP(0.f);
        depth = Int32P(0);
        specularBounce = MaskP(false);
        alive = MaskP(true);
    }
};

// =======================================================================
// HitP intersection result (Embree hit -> shading lookup)
// =======================================================================

// The result of tracing a RayP packet against the Embree scene.
// b1/b2 are the barycentric coordinates.
struct HitP {
    UInt32P geomID;  ///< Mesh / geometry index
    UInt32P primID;  ///< Triangle index within the geometry
    FloatP t;        ///< Hit distance along the ray
    FloatP b1;       ///< First barycentric coordinate
    FloatP b2;       ///< Second barycentric coordinate

    /// True where the packet ray actually hit something (t < maxt).
    MaskP IsValid(const FloatP &maxt) const {
        return (t >= FloatP(0.f)) & (t < maxt);
    }
};

} // namespace lux2

#endif // LUX2_RAY_H
