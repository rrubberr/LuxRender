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

#include "filters/mitchell.h"

#include "core/dynload.h"
#include "core/paramset.h"
#include "core/register.h"

namespace lux2
{

    std::shared_ptr<Filter> MitchellFilter::CreateFilter(const PluginContext &ctx)
    {
        const float xw = ctx.params ? ctx.params->FindOneFloat("xwidth", 2.f) : 2.f;
        const float yw = ctx.params ? ctx.params->FindOneFloat("ywidth", 2.f) : 2.f;
        const float B = ctx.params ? ctx.params->FindOneFloat("B", 1.f / 3.f) : 1.f / 3.f;
        const float C = ctx.params ? ctx.params->FindOneFloat("C", 1.f / 3.f) : 1.f / 3.f;
        // Lux's "supersample" is a radial three-tap blend that we can't do here.
        if (ctx.params)
            ctx.params->FindOneBool("supersample", false);
        return std::make_shared<MitchellFilter>(xw, yw, B, C);
    }

    LUX2_REGISTER_FILTER(MitchellFilter, "mitchell");

} // namespace lux2
