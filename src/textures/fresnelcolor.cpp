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

#include "textures/fresnelcolor.h"

#include "core/dynload.h"
#include "core/paramset.h"
#include "core/register.h"
#include "core/texture.h"

namespace lux2
{

    std::shared_ptr<FresnelTexture> FresnelColorTexture::CreateFresnelTexture(
        const PluginContext &ctx)
    {
        std::shared_ptr<ColorTexture> color;

        // Look up in the color texture table.
        const std::string texName = ctx.params->FindTexture("Kr");
        if (!texName.empty() && ctx.colorTextures)
        {
            auto it = ctx.colorTextures->find(texName);
            if (it != ctx.colorTextures->end())
                color = it->second;
        }

        // Build a constant from the RGB value.
        if (!color)
        {
            const RGBColor &rgb = ctx.params->FindOneRGBColor("Kr", RGBColor(0.5f));
            const RGBColorP rgbP(FloatP(rgb.r()), FloatP(rgb.g()), FloatP(rgb.b()));
            color = std::make_shared<ConstantColorTexture>(rgbP);
        }

        return std::make_shared<FresnelColorTexture>(std::move(color));
    }

    LUX2_REGISTER_FRESNEL_TEXTURE(FresnelColorTexture, "fresnelcolor");

} // namespace lux2
