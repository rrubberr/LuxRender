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

#ifndef LUX2_TEXTURE_CONSTANT_H
#define LUX2_TEXTURE_CONSTANT_H

#include "core/texture.h"

#include <memory>

namespace lux2
{

    class PluginContext;

    // A float texture returning a single constant value.
    class ConstantFloatTexture : public FloatTexture
    {
    public:
        explicit ConstantFloatTexture(const FloatP &value) : m_value(value) {}

        FloatP Evaluate(const DifferentialGeometryP &, const SpectrumWavelengthsP &,
                        MaskP = MaskP(true)) const override
        {
            return m_value;
        }

        bool IsConstant() const override { return true; }

        static std::shared_ptr<FloatTexture> CreateFloatTexture(const PluginContext &ctx);

    private:
        FloatP m_value;
    };

    // A fresnel texture returning a constant dielectric IOR.
    class ConstantFresnelTexture : public FresnelTexture
    {
    public:
        explicit ConstantFresnelTexture(const FloatP &value) : m_value(value) {}

        FresnelGeneralP Evaluate(const DifferentialGeometryP &,
                                 const SpectrumWavelengthsP &,
                                 MaskP = MaskP(true)) const override
        {
            FresnelGeneralP f;
            f.eta = m_value;
            f.k = FloatP(0.f);
            f.model = FresnelModel::Dielectric;
            return f;
        }

        bool IsConstant() const override { return true; }

        static std::shared_ptr<FresnelTexture> CreateFresnelTexture(const PluginContext &ctx);

    private:
        FloatP m_value;
    };

} // namespace lux2

#endif // LUX2_TEXTURE_CONSTANT_H
