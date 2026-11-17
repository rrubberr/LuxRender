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

// Light selection for a light subpath origin.

#ifndef LUX2_INTEGRATORS_LANE_LIGHT_PICK_H
#define LUX2_INTEGRATORS_LANE_LIGHT_PICK_H

#include "integrators/lane/tile_ctx.h"

#include "core/light_call.h"
#include "core/renderinghints.h"

#include <enoki/array.h>

namespace lux2
{
    struct LightOriginPick
    {
        LightPtr light;
        UInt32P idx; // index into TileCtx::lightPtrs
        FloatP pSel; // selection density of that light
    };

    // slot indexes the task table under StrategyKind::All or is ignored.
    inline LightOriginPick PickLight(const TileCtx &c,
                                     const LightsSamplingStrategy &ls,
                                     uint32_t slot, const FloatP &u,
                                     const MaskP &gate)
    {
        LightOriginPick r;
        if (ls.kind == LightsSamplingStrategy::StrategyKind::All)
        {
            const uint32_t k = ls.task[slot];
            r.light = c.lightPtrs[k];
            r.idx = UInt32P(k);
            r.pSel = FloatP(ls.pSel[k]);
        }
        else
        {
            const LightPick pick = SampleLight(ls, u, gate);
            r.light =
                enoki::gather<LightPtr>(c.lightPtrs.data(), pick.idx, gate);
            r.idx = pick.idx;
            r.pSel = pick.pSel;
        }
        return r;
    }

    // Variant for StrategyKind::All.
    inline LightOriginPick PickLightPerSlot(const TileCtx &c,
                                            const LightsSamplingStrategy &ls,
                                            const UInt32P &slot,
                                            const MaskP &gate)
    {
        LightOriginPick r;
        const UInt32P k = enoki::gather<UInt32P>(ls.task.data(), slot, gate);
        r.light = enoki::gather<LightPtr>(c.lightPtrs.data(), k, gate);
        r.idx = k;
        r.pSel = enoki::gather<FloatP>(ls.pSel.data(), k, gate);
        return r;
    }

} // namespace lux2

#endif // LUX2_INTEGRATORS_LANE_LIGHT_PICK_H
