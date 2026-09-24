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

#ifndef LUX2_MATERIAL_METAL2_H
#define LUX2_MATERIAL_METAL2_H

#include "core/material.h"
#include "core/bsdf.h"
#include "core/bsdf_type.h"
#include "core/texture.h"

#include <memory>

namespace lux2
{

    class PluginContext;

    class Metal2Material : public Material, public BSDF
    {
    public:
        Metal2Material(std::shared_ptr<FresnelTexture> fr,
                       std::shared_ptr<FloatTexture> nu,
                       std::shared_ptr<FloatTexture> nv)
            : m_fr(std::move(fr)), m_nu(std::move(nu)), m_nv(std::move(nv)) {}

        uint32_t flags() const override
        {
            return uint32_t(BSDFType::GlossyReflection) |
                   uint32_t(BSDFType::FrontSide);
        }

        const BSDF *GetBSDF(const DifferentialGeometryP &) const override { return this; }

        void SampleF(const SpectrumWavelengthsP &sw, const Vector3fP &wo,
                     const DifferentialGeometryP &dg, const FloatP &u0,
                     const FloatP &u1, const FloatP &, BSDFSampleP *s,
                     TransportMode, MaskP active) const override;

        FloatP Pdf(const SpectrumWavelengthsP &, const Vector3fP &wi,
                   const Vector3fP &wo, const DifferentialGeometryP &dg,
                   uint32_t, TransportMode, MaskP active) const override;

        static std::shared_ptr<Material> CreateMaterial(const PluginContext &ctx);

    private:
        std::shared_ptr<FresnelTexture> m_fr;
        std::shared_ptr<FloatTexture> m_nu;
        std::shared_ptr<FloatTexture> m_nv;
    };

} // namespace lux2

#endif // LUX2_MATERIAL_METAL2_H
