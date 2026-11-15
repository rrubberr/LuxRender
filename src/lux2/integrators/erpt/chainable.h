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

// Chain eligibility.

#ifndef LUX2_INTEGRATORS_ERPT_CHAINABLE_H
#define LUX2_INTEGRATORS_ERPT_CHAINABLE_H

#include "integrators/wf/types.h"

#include "core/bsdf.h"
#include "core/bsdf_type.h"
#include "core/bsdfptr_table.h"

namespace lux2
{
    namespace erpt
    {
        // A hit usable as a chain scatter vertex.
        inline MaskP ChainableHit(const WalkCtx &c, const HitP &h, const MaskP &act)
        {
            MaskP ok = act && h.hit &&
                       eq(h.lightID, UInt32P(LUX_NO_LIGHT));
            if (!any(ok))
                return ok;
            const BSDFPtr b = c.table->Gather(h.matID, ok);
            const auto fl = b->flags(ok);
            ok = ok && !has_flag(fl, BSDFType::Null) &&
                 !has_flag(fl, BSDFType::Specular);
            return ok;
        }

        // Scalar.
        inline bool ChainableMat(const WalkCtx &c, uint32_t matID)
        {
            if (matID >= c.table->ptrs.size())
                return false;
            const BSDF *b = c.table->ptrs[matID];
            if (!b)
                return false;
            const uint32_t fl = b->flags();
            // Plain bitwise.
            return (fl & uint32_t(BSDFType::Null)) == 0u &&
                   (fl & uint32_t(BSDFType::Specular)) == 0u;
        }

    } // namespace erpt
} // namespace lux2

#endif // LUX2_INTEGRATORS_ERPT_CHAINABLE_H
