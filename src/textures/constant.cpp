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

#include "textures/constant.h"

#include "core/dynload.h"
#include "core/paramset.h"
#include "core/register.h"

namespace lux2
{

    std::shared_ptr<FloatTexture> ConstantFloatTexture::CreateFloatTexture(
        const PluginContext &ctx)
    {
        const float v = ctx.params->FindOneFloat("value", 1.f);
        return std::make_shared<ConstantFloatTexture>(FloatP(v));
    }

    std::shared_ptr<FresnelTexture> ConstantFresnelTexture::CreateFresnelTexture(
        const PluginContext &ctx)
    {
        const float v = ctx.params->FindOneFloat("value", 1.f);
        return std::make_shared<ConstantFresnelTexture>(FloatP(v));
    }

    class ConstantColorTextureFactory
    {
    public:
        static std::shared_ptr<ColorTexture> CreateColorTexture(const PluginContext &ctx)
        {
            const RGBColor &rgb = ctx.params->FindOneRGBColor("value", RGBColor(1.f));
            const RGBColorP rgbP(FloatP(rgb.r()), FloatP(rgb.g()), FloatP(rgb.b()));
            return std::make_shared<ConstantColorTexture>(rgbP);
        }
    };

    LUX2_REGISTER_FLOAT_TEXTURE(ConstantFloatTexture, "constant");
    LUX2_REGISTER_COLOR_TEXTURE(ConstantColorTextureFactory, "constant");
    LUX2_REGISTER_FRESNEL_TEXTURE(ConstantFresnelTexture, "constant");

} // namespace lux2
