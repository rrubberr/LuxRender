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

#ifndef LUX2_LIGHT_AREA_H
#define LUX2_LIGHT_AREA_H

#include "core/light.h"
#include "core/shape.h"
#include "core/texture.h"

#include <memory>
#include <vector>

namespace lux2
{

    struct PluginContext;

    class AreaLight : public Light
    {
    public:
        AreaLight(std::shared_ptr<ColorTexture> Le, float gain);

        uint32_t flags() const override;
        bool IsInfinite() const override { return false; }
        UInt group() const override { return m_group; }

        // Radiance emitted along a ray that hit this emitter.
        SWCSpectrumP Le(const RayP &ray, MaskP active = MaskP(true)) const override;

        MaskP Sample_L(const SpectrumWavelengthsP &sw,
                       const Point3fP &p, const Normal3fP &n,
                       const FloatP &u0, const FloatP &u1, const FloatP &u2,
                       Point3fP *lightP, Vector3fP *wi, Normal3fP *lightN,
                       FloatP *pdf, SWCSpectrumP *LeOut,
                       MaskP active = MaskP(true)) const override;

        FloatP Pdf_L(const Point3fP &p, const Normal3fP &n,
                     const Point3fP &lightP, const Normal3fP &lightN,
                     MaskP active = MaskP(true)) const override;

        // Bind the emitting triangles and build the area CDF.
        void BindGeometry(const std::vector<TriangleDesc> &tris);

        // Total emitting area.
        float TotalArea() const { return m_totalArea; }

        static std::shared_ptr<Light> CreateLight(const PluginContext &ctx);

    private:
        std::shared_ptr<ColorTexture> m_Le;
        float m_gain;
        UInt m_group = 0;

        // Emitting-triangle tables, for gather-based sampling.
        std::vector<float> m_v0x, m_v0y, m_v0z;
        std::vector<float> m_v1x, m_v1y, m_v1z;
        std::vector<float> m_v2x, m_v2y, m_v2z;
        std::vector<float> m_n0x, m_n0y, m_n0z;
        std::vector<float> m_n1x, m_n1y, m_n1z;
        std::vector<float> m_n2x, m_n2y, m_n2z;
        std::vector<float> m_cdf; // Cumulative per-triangle areas.
        float m_totalArea = 0.f;
        std::uint32_t m_triCount = 0;
    };

} // namespace lux2

#endif // LUX2_LIGHT_AREA_H
