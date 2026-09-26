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

#ifndef LUX2_LIGHT_INFINITE_H
#define LUX2_LIGHT_INFINITE_H

#include "core/light.h"
#include "core/texture.h"

#include <memory>

namespace lux2
{

    struct PluginContext;

    class InfiniteLight : public Light
    {
    public:
        InfiniteLight(std::shared_ptr<ColorTexture> Le, float gain);

        uint32_t flags() const override;
        bool IsInfinite() const override { return true; }
        UInt group() const override { return m_group; }

        // Radiance arriving from the ray's direction.
        SWCSpectrumP Le(const RayP &ray, MaskP active = MaskP(true)) const override;

        // Used only if an eye ray somehow reports a surface hit on the env.
        SWCSpectrumP Le(const DifferentialGeometryP &dg,
                        const SpectrumWavelengthsP &sw,
                        MaskP active = MaskP(true)) const override;

        MaskP Sample_L(const SpectrumWavelengthsP &sw,
                       const Point3fP &p, const Normal3fP &n,
                       const FloatP &u0, const FloatP &u1, const FloatP &u2,
                       Point3fP *lightP, Vector3fP *wi, Normal3fP *lightN,
                       FloatP *pdf, SWCSpectrumP *LeOut,
                       MaskP active = MaskP(true)) const override;

        // pdf is constant and independent of lightP/lightN.
        FloatP Pdf_L(const Point3fP &p, const Normal3fP &n,
                     const Point3fP &lightP, const Normal3fP &lightN,
                     MaskP active = MaskP(true)) const override;

        static std::shared_ptr<Light> CreateLight(const PluginContext &ctx);

    private:
        std::shared_ptr<ColorTexture> m_Le;
        float m_gain;
        UInt m_group = 0;
    };

} // namespace lux2

#endif // LUX2_LIGHT_INFINITE_H
