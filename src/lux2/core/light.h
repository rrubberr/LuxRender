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

#ifndef LUX2_LIGHT_H
#define LUX2_LIGHT_H

#include "core/math/vecp.h"
#include "core/math/geometry.h"
#include "core/color/spectrum.h"
#include "core/color/color.h"
#include "core/bsdf.h"
#include "core/bsdf_type.h"
#include "core/math/ray.h"

#include <enoki/array.h>

namespace lux2
{

    // Abstract light source.
    class Light
    {
    public:
        ENOKI_CALL_SUPPORT_FRIEND()

        virtual ~Light() = default;

        // Lobe types this light emits into for MIS weighting.
        virtual uint32_t flags() const = 0;

        // Infinite lights have no finite sample position.
        virtual bool IsInfinite() const = 0;

        // Total emitting surface area (m^2).
        virtual float TotalArea() const { return 0.f; }

        // Total emitted luminous flux, for power/importance weighting.
        virtual float Power() const { return 1.f; }

        // Importance hint from the lxs param.
        float Importance() const { return m_importance; }
        void SetImportance(float v) { m_importance = v; }

        // Light group index.
        virtual UInt group() const = 0;

        // Assign light's group index.
        void SetGroup(UInt g) { m_group = g; }

        // Radiance emitted along `ray`.
        virtual SWCSpectrumP Le(const RayP &ray, MaskP active = MaskP(true)) const = 0;

        // wo is the direction from the hitpoint towards the ray origin (-ray.d).
        // Area lights are one sided so must return 0 where shading normal faces
        // away from wo; infinite lights ignore.
        virtual SWCSpectrumP Le(const DifferentialGeometryP &dg,
                                const SpectrumWavelengthsP &sw,
                                const Vector3fP &wo,
                                MaskP active = MaskP(true)) const = 0;

        // receiverHasNormal is per lane.

        // NEE / photon-emission position sampler.
        virtual MaskP Sample_Li(const SpectrumWavelengthsP &sw,
                                const Point3fP &p, const Normal3fP &n,
                                const FloatP &u0, const FloatP &u1,
                                const FloatP &u2,
                                Point3fP *lightP, Vector3fP *wi,
                                Normal3fP *lightN,
                                FloatP *pdf, SWCSpectrumP *Le,
                                MaskP active = MaskP(true),
                                MaskP receiverHasNormal = MaskP(true)) const = 0;

        // PDF of sampling the point lightP on this light, seen
        // from p with shading normal n. Used for MIS.
        virtual FloatP Pdf_L(const Point3fP &p, const Normal3fP &n,
                             const Point3fP &lightP, const Normal3fP &lightN,
                             MaskP active = MaskP(true),
                             MaskP receiverHasNormal = MaskP(true)) const = 0;

        // Sample an emission origin for light path construction.
        virtual MaskP Sample_Le(const SpectrumWavelengthsP &sw,
                                const FloatP &u0, const FloatP &u1,
                                const FloatP &u2,
                                BSDFPtr *bsdf, DifferentialGeometryP *dg,
                                FloatP *pdfArea, SWCSpectrumP *Le,
                                MaskP active = MaskP(true)) const
        {
            (void)sw;
            (void)u0;
            (void)u1;
            (void)u2;
            (void)bsdf;
            (void)dg;
            enoki::masked(*pdfArea, active) = FloatP(0.f);
            return MaskP(false);
        }

    protected:
        // Group index backing the default group() override.
        UInt m_group = 0;
        float m_importance = 1.f; // RenderingHints default
    };

} // namespace lux2

#endif // LUX2_LIGHT_H
