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

#include "media/homogeneous.h"

#include "core/color.h"
#include "core/dynload.h"
#include "core/paramset.h"
#include "core/register.h"

namespace lux2
{
    std::shared_ptr<Medium> HomogeneousMedium::CreateMedium(
        const PluginContext &ctx)
    {
        ParamSet *p = ctx.params;
        const RGBColor sa =
            p ? p->FindOneRGBColor("sigma_a", RGBColor(0.f)) : RGBColor(0.f);
        const RGBColor ss =
            p ? p->FindOneRGBColor("sigma_s", RGBColor(0.f)) : RGBColor(0.f);
        const float g = p ? p->FindOneFloat("g", 0.f) : 0.f;
        const RGBColor le =
            p ? p->FindOneRGBColor("Le", RGBColor(0.f)) : RGBColor(0.f);

        // p0/p1 is a region concept applied by the scene's
        // region path via SetBound.
        return std::make_shared<HomogeneousMedium>(sa, ss, g, le);
    }

} // namespace lux2

LUX2_REGISTER_MEDIUM(lux2::HomogeneousMedium, "homogeneous");
