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
#include "core/fresnel.h"
#include "core/texture.h"

#include <memory>

namespace lux2
{

    class PluginContext;

    // Specular glass is a reflection & transmission delta BSDF with
    // weighted component selection, Cauchy dispersion, thin-film
    // on reflection, and a no-refraction mode.

    class GlassMaterial : public Material, public BSDF
    {
    public:
        GlassMaterial(std::shared_ptr<ColorTexture> Kr,
                      std::shared_ptr<ColorTexture> Kt,
                      std::shared_ptr<FloatTexture> index,
                      std::shared_ptr<FloatTexture> cauchyb,
                      std::shared_ptr<FloatTexture> film,
                      std::shared_ptr<FloatTexture> filmindex,
                      bool architectural)
            : m_Kr(std::move(Kr)), m_Kt(std::move(Kt)),
              m_index(std::move(index)), m_cauchyb(std::move(cauchyb)),
              m_film(std::move(film)), m_filmindex(std::move(filmindex)),
              m_architectural(architectural)
        {
        }

        uint32_t flags() const override
        {
            return uint32_t(BSDFType::SpecularReflection) |
                   uint32_t(BSDFType::SpecularTransmission) |
                   uint32_t(BSDFType::FrontSide) |
                   uint32_t(BSDFType::BackSide);
        }

        const BSDF *GetBSDF(const DifferentialGeometryP &) const override
        {
            return this;
        }

        void SampleF(const SpectrumWavelengthsP &sw, const Vector3fP &wo,
                     const DifferentialGeometryP &dg, const FloatP &u0,
                     const FloatP &u1, const FloatP &u2, BSDFSampleP *sample,
                     TransportMode mode, MaskP active) const override;

        // Both lobes are delta so no finite solid-angle density.
        FloatP Pdf(const SpectrumWavelengthsP &, const Vector3fP &, const Vector3fP &,
                   const DifferentialGeometryP &, uint32_t, TransportMode,
                   MaskP) const override
        {
            return FloatP(0.f);
        }

        // See the NEE comment in glass.cpp.
        void Eval(const SpectrumWavelengthsP &sw, const Vector3fP &wi,
                  const Vector3fP &wo, const DifferentialGeometryP &dg,
                  TransportMode mode, BSDFEvalP *out,
                  MaskP active) const override;

        static std::shared_ptr<Material> CreateMaterial(const PluginContext &ctx);

    private:
        // Monochromatic lane Cauchy IOR + Fresnel at signed cosi (signed for lux
        // two sided FresnelCauchy behavior, TIR yields F = 1).
        FloatP FresnelAt(const SpectrumWavelengthsP &sw,
                         const DifferentialGeometryP &dg, const FloatP &cosi,
                         MaskP active) const;

        std::shared_ptr<ColorTexture> m_Kr;
        std::shared_ptr<ColorTexture> m_Kt;
        std::shared_ptr<FloatTexture> m_index;
        std::shared_ptr<FloatTexture> m_cauchyb;
        std::shared_ptr<FloatTexture> m_film;
        std::shared_ptr<FloatTexture> m_filmindex;
        bool m_architectural;
    };

} // namespace lux2

#endif // LUX2_MATERIAL_GLASS_H
