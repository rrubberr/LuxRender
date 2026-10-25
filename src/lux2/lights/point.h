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

#ifndef LUX2_LIGHT_POINT_H
#define LUX2_LIGHT_POINT_H

#include "core/light.h"
#include "core/texture.h"
#include "core/math/vecp.h"

#include <memory>

namespace lux2
{

    struct PluginContext;

    // Isotropic point light, a positional delta emitter.
    class PointLight : public Light
    {
    public:
        // power/efficacy normalize emission to a luminous flux.
        PointLight(std::shared_ptr<ColorTexture> L, float baseGain,
                   float power, float efficacy, const Point3f &worldPos);

        uint32_t flags() const override
        {
            // Delta lights emit into every lobe for MIS weighting.
            return uint32_t(BSDFType::All);
        }
        bool IsInfinite() const override { return false; }
        UInt group() const override { return m_group; }

        // Total emitted luminous flux.
        float Power() const override;

        // A point has no surface.
        SWCSpectrumP Le(const RayP &, MaskP) const override
        {
            return SWCSpectrumP(0.f);
        }
        SWCSpectrumP Le(const DifferentialGeometryP &,
                        const SpectrumWavelengthsP &, const Vector3fP &,
                        MaskP) const override
        {
            return SWCSpectrumP(0.f);
        }

        MaskP Sample_Li(const SpectrumWavelengthsP &sw, const Point3fP &p,
                       const Normal3fP &n, const FloatP &u0, const FloatP &u1,
                       const FloatP &u2, Point3fP *lightP, Vector3fP *wi,
                       Normal3fP *lightN, FloatP *pdf, SWCSpectrumP *LeOut,
                       MaskP active = MaskP(true),
                       MaskP receiverHasNormal = MaskP(true)) const override;

        // Dirac positional light without solid angle density.
        FloatP Pdf_L(const Point3fP &, const Normal3fP &, const Point3fP &,
                     const Normal3fP &, MaskP = MaskP(true),
                     MaskP = MaskP(true)) const override
        {
            return FloatP(0.f);
        }

        static std::shared_ptr<Light> CreateLight(const PluginContext &ctx);

    private:
        std::shared_ptr<ColorTexture> m_L;
        float m_gain;  // effective = baseGain * photometric normalization
        Point3f m_pos; // world space position
    };

} // namespace lux2

#endif // LUX2_LIGHT_POINT_H
