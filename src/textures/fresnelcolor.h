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

#ifndef LUX2_TEXTURE_FRESNELCOLOR_H
#define LUX2_TEXTURE_FRESNELCOLOR_H

#include "core/texture.h"
#include "core/fresnel.h"

#include <memory>

namespace lux2
{

    class PluginContext;

    class FresnelColorTexture : public FresnelTexture
    {
    public:
        explicit FresnelColorTexture(std::shared_ptr<ColorTexture> color)
            : m_color(std::move(color)) {}

        FresnelGeneralP Evaluate(const DifferentialGeometryP &dg,
                                 const SpectrumWavelengthsP &sw,
                                 MaskP active) const override
        {
            const SWCSpectrumP c = m_color->Evaluate(dg, sw, active);
            FresnelGeneralP f;
            f.eta = FresnelApproxEta(c);
            f.k = FresnelApproxK(c);
            f.model = FresnelModel::Full;
            return f;
        }

        const std::shared_ptr<ColorTexture> &GetColorTexture() const { return m_color; }

        static std::shared_ptr<FresnelTexture> CreateFresnelTexture(const PluginContext &ctx);

    private:
        std::shared_ptr<ColorTexture> m_color;
    };

} // namespace lux2

#endif // LUX2_TEXTURE_FRESNELCOLOR_H
