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

#ifndef LUX2_MEDIUM_H
#define LUX2_MEDIUM_H

#include "core/vecp.h"
#include "core/geometry.h"
#include "core/spectrum.h"
#include "core/color.h"
#include "core/bbox.h"
#include "core/ray.h"
#include "core/rng.h"

#include <cstdint>
#include <limits>

namespace lux2
{

    // ---------------------------------------------------------------------------
    // MediumFlags
    // ---------------------------------------------------------------------------

    enum MediumFlags : uint32_t
    {
        MF_Analytic = 1, // Closed form tau/flight (homogeneous, exponential)
        MF_Tracked = 2,  // DDA and null-collision tracking (heterogeneous, grid)
        MF_Emissive = 4, // Lve != 0 somewhere
        MF_Bounded = 8   // Has a finite AABB (region, mesh)
    };

    // ---------------------------------------------------------------------------
    // MediumSampleP
    // ---------------------------------------------------------------------------

    struct MediumSampleP
    {
        FloatP d;              // Event distance from t0
        MaskP scattered;       // True where a scattering event occurred
        SWCSpectrumP weight;   // Throughput factor after the event
        SWCSpectrumP emission; // Radiance gathered over the flight
        SWCSpectrumP sigma_s;  // Scattering coefficient at the event
        SWCSpectrumP sigma_t;  // Total coefficient at the event
    };

    // Reset a MediumSampleP to the "escape, no event" state.
    inline void ClearMediumSample(MediumSampleP *s, const FloatP &t1)
    {
        s->d = t1;
        s->scattered = MaskP(false);
        s->weight = SWCSpectrumP(1.f);
        s->emission = SWCSpectrumP(0.f);
        s->sigma_s = SWCSpectrumP(0.f);
        s->sigma_t = SWCSpectrumP(0.f);
    }

    // ---------------------------------------------------------------------------
    // Medium abstract interface
    // ---------------------------------------------------------------------------

    class Medium
    {
    public:
        virtual ~Medium() = default;

        // Bitwise OR of MediumFlags.
        virtual uint32_t Flags() const = 0;

        // Bounding box in world space.
        virtual BBox Bounds() const { return BBox(); }

        // -----------------------------------------------------------------
        // Coefficient queries at a point
        // -----------------------------------------------------------------

        virtual SWCSpectrumP SigmaA(const SpectrumWavelengthsP &sw,
                                    const Point3fP &p,
                                    MaskP active) const = 0;

        virtual SWCSpectrumP SigmaS(const SpectrumWavelengthsP &sw,
                                    const Point3fP &p,
                                    MaskP active) const = 0;

        // Emitted radiance per unit length at point p.
        virtual SWCSpectrumP Lve(const SpectrumWavelengthsP &sw,
                                 const Point3fP &p,
                                 MaskP active) const
        {
            return SWCSpectrumP(0.f);
        }

        // -----------------------------------------------------------------
        // Fused flight sampling
        // -----------------------------------------------------------------

        // Sample an interaction within [t0, t1].
        virtual void SampleInteraction(const SpectrumWavelengthsP &sw,
                                       const RayP &ray,
                                       const FloatP &t0,
                                       const FloatP &t1,
                                       const FloatP &u_flight,
                                       RNGP &rng,
                                       MediumSampleP *out,
                                       MaskP active) const = 0;

        // -----------------------------------------------------------------
        // Transmittance over [t0, t1]
        // -----------------------------------------------------------------

        // Closed form or ratio tracking.
        virtual SWCSpectrumP Transmittance(
            const SpectrumWavelengthsP &sw,
            const RayP &ray,
            const FloatP &t0,
            const FloatP &t1,
            RNGP &rng,
            MaskP active) const = 0;

        // -----------------------------------------------------------------
        // Optical depth
        // -----------------------------------------------------------------

        virtual SWCSpectrumP Tau(const RayP &ray,
                                 const FloatP &t0,
                                 const FloatP &t1,
                                 MaskP active) const
        {
            return SWCSpectrumP(0.f);
        }

        // -----------------------------------------------------------------
        // Phase function (Schlick)
        // -----------------------------------------------------------------

        // Sample the phase function.
        virtual void PhaseSample(const Vector3fP &wo,
                                 const FloatP &u1,
                                 const FloatP &u2,
                                 Vector3fP *wi,
                                 FloatP *pdf,
                                 MaskP active) const = 0;

        // Evaluate the phase pdf for a (wo, wi) pair.
        virtual FloatP PhasePdf(const Vector3fP &wo,
                                const Vector3fP &wi,
                                MaskP active) const = 0;

        // -----------------------------------------------------------------
        // Bounding volume intersection
        // -----------------------------------------------------------------

        // Writes entry/exit distances and a hit mask.
        virtual void IntersectAABB(const RayP &ray,
                                   FloatP *t0,
                                   FloatP *t1,
                                   MaskP *hit,
                                   MaskP active) const
        {
            *t0 = ray.mint;
            *t1 = FloatP(std::numeric_limits<float>::infinity());
            *hit = active;
        }

        ENOKI_CALL_SUPPORT_FRIEND()
    };

    // Type for enoki::call dispatch.
    using MediumPtr = enoki::replace_scalar_t<FloatP, const Medium *>;

} // namespace lux2

#endif // LUX2_MEDIUM_H
