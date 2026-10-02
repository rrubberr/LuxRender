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

#ifndef LUX2_MATERIAL_UTIL_H
#define LUX2_MATERIAL_UTIL_H

#include "core/color.h"
#include "core/dynload.h"
#include "core/paramset.h"
#include "core/texture.h"

#include <memory>
#include <string>

namespace lux2
{

    // Resolve a named float texture or fall back to constant.
    inline std::shared_ptr<FloatTexture> getFloatTex(const PluginContext &ctx,
                                                     const char *name,
                                                     float def)
    {
        const std::string texName =
            ctx.params ? ctx.params->FindTexture(name) : "";
        if (!texName.empty() && ctx.floatTextures)
        {
            auto it = ctx.floatTextures->find(texName);
            if (it != ctx.floatTextures->end())
                return it->second;
        }
        const float v = ctx.params ? ctx.params->FindOneFloat(name, def) : def;
        return std::make_shared<ConstantTexture<FloatP>>(FloatP(v));
    }

    // Resolve a named color texture or fall back to constant RGB.
    inline std::shared_ptr<ColorTexture> getColorTex(const PluginContext &ctx,
                                                     const char *name,
                                                     const RGBColor &def)
    {
        const std::string texName =
            ctx.params ? ctx.params->FindTexture(name) : "";
        if (!texName.empty() && ctx.colorTextures)
        {
            auto it = ctx.colorTextures->find(texName);
            if (it != ctx.colorTextures->end())
                return it->second;
        }
        const RGBColor rgb =
            ctx.params ? ctx.params->FindOneRGBColor(name, def) : def;
        return std::make_shared<ConstantColorTexture>(
            RGBColorP(FloatP(rgb.r()), FloatP(rgb.g()), FloatP(rgb.b())));
    }

    // Resolve a named fresnel texture or fall back to a constant eta with
    // k == 0 (model Auto).
    inline std::shared_ptr<FresnelTexture> getFresnelTex(
        const PluginContext &ctx, const char *name, float def)
    {
        const std::string texName =
            ctx.params ? ctx.params->FindTexture(name) : "";
        if (!texName.empty() && ctx.fresnelTextures)
        {
            auto it = ctx.fresnelTextures->find(texName);
            if (it != ctx.fresnelTextures->end())
                return it->second;
        }
        const float v = ctx.params ? ctx.params->FindOneFloat(name, def) : def;
        FresnelGeneralP fg;
        fg.eta = FloatP(v);
        fg.k = FloatP(0.f);
        fg.model = FresnelModel::Auto;
        return std::make_shared<ConstantTexture<FresnelGeneralP>>(fg);
    }

} // namespace lux2

#endif // LUX2_MATERIAL_UTIL_H
