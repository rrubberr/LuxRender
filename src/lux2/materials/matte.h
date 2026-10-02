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

#ifndef LUX2_MATERIAL_MATTE_H
#define LUX2_MATERIAL_MATTE_H

#include "core/material.h"
#include "core/bsdf.h"
#include "core/bsdf_type.h"
#include "core/math.h"
#include "core/sampling.h"
#include "core/texture.h"

#include <memory>

namespace lux2
{

    class PluginContext;

    // Matte is a single diffuse reflection lobe that's Lambertian when sigma == 0
    // and Oren-Nayar otherwise. Lux picks the lobe in GetBSDF serially but Lux2
    // evaluates the Oren-Nayar form with per-lane A/B coefficients to reduce to
    // Lambertian at sigma == 0.

    class MatteMaterial : public Material, public BSDF
    {
    public:
        MatteMaterial(std::shared_ptr<ColorTexture> Kd,
                      std::shared_ptr<FloatTexture> sigma)
            : m_Kd(std::move(Kd)), m_sigma(std::move(sigma))
        {
        }

        uint32_t flags() const override
        {
            return uint32_t(BSDFType::DiffuseReflection) |
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

        // Lux BxDF::Pdf cosine density solid angle pdf where
        // SameHemisphere ? |cos(wi)| * INVPI : 0. No ng side test, because
        // Lux SingleBSDF::Pdf only checks the lobe.
        FloatP Pdf(const SpectrumWavelengthsP &, const Vector3fP &wi,
                   const Vector3fP &wo, const DifferentialGeometryP &dg,
                   uint32_t, TransportMode, MaskP) const override
        {
            return select(sameHemisphere(wo, wi, dg.n),
                          abs(dot(wi, dg.n)) * INVPI, FloatP(0.f));
        }

        // Lux Lambertian/OrenNayar F() is non-empty. f carries
        // |cos(light)|; pdf/pdfRev are the solid-angle densities for MIS.
        void Eval(const SpectrumWavelengthsP &sw, const Vector3fP &wi,
                  const Vector3fP &wo, const DifferentialGeometryP &dg,
                  TransportMode mode, BSDFEvalP *out,
                  MaskP active) const override;

        static std::shared_ptr<Material> CreateMaterial(const PluginContext &ctx);

    private:
        // Oren-Nayar coefficients from a [0,90] clamped sigma texture.
        void coefficients(const DifferentialGeometryP &dg,
                          const SpectrumWavelengthsP &sw, MaskP active,
                          SWCSpectrumP *R, FloatP *A, FloatP *B) const;

        std::shared_ptr<ColorTexture> m_Kd;
        std::shared_ptr<FloatTexture> m_sigma;
    };

} // namespace lux2

#endif // LUX2_MATERIAL_MATTE_H
