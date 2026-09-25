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

#ifndef LUX2_MATERIAL_GLASS_H
#define LUX2_MATERIAL_GLASS_H

#include "core/material.h"
#include "core/bsdf.h"
#include "core/bsdf_type.h"
#include "core/texture.h"

#include <memory>

namespace lux2
{

    class PluginContext;

    class GlassMaterial : public Material, public BSDF
    {
    public:
        GlassMaterial(std::shared_ptr<ColorTexture> kr,
                      std::shared_ptr<ColorTexture> kt,
                      std::shared_ptr<FloatTexture> index)
            : m_kr(std::move(kr)), m_kt(std::move(kt)), m_index(std::move(index)) {}

        uint32_t flags() const override
        {
            return uint32_t(BSDFType::SpecularReflection) |
                   uint32_t(BSDFType::SpecularTransmission) |
                   uint32_t(BSDFType::FrontSide) |
                   uint32_t(BSDFType::BackSide);
        }

        const BSDF *GetBSDF(const DifferentialGeometryP &) const override { return this; }

        void SampleF(const SpectrumWavelengthsP &sw, const Vector3fP &wo,
                     const DifferentialGeometryP &dg, const FloatP &u0,
                     const FloatP &u1, const FloatP &u2, BSDFSampleP *s,
                     TransportMode, MaskP active) const override;

        // Continuous pdf is 0; the integrator special-cases
        // sampledType & Specular.
        FloatP Pdf(const SpectrumWavelengthsP &, const Vector3fP &, const Vector3fP &,
                   const DifferentialGeometryP &, uint32_t, TransportMode,
                   MaskP) const override
        {
            return FloatP(0.f);
        }

        // Delta/specular lobes have no finite pdf so NEE never queries Eval.
        void Eval(const SpectrumWavelengthsP &, const Vector3fP &, const Vector3fP &,
                  const DifferentialGeometryP &, TransportMode, BSDFEvalP *out,
                  MaskP active) const override
        {
            enoki::masked(out->f, active) = SWCSpectrumP(0.f);
            enoki::masked(out->pdf, active) = FloatP(0.f);
            enoki::masked(out->pdfRev, active) = FloatP(0.f);
        }

        static std::shared_ptr<Material> CreateMaterial(const PluginContext &ctx);

    private:
        std::shared_ptr<ColorTexture> m_kr;
        std::shared_ptr<ColorTexture> m_kt;
        std::shared_ptr<FloatTexture> m_index;
    };

} // namespace lux2

#endif // LUX2_MATERIAL_GLASS_H
