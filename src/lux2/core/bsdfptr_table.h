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

#ifndef LUX2_BSDFPTR_TABLE_H
#define LUX2_BSDFPTR_TABLE_H

#include "core/bsdf.h"
#include "core/bsdf_call.h"

#include <enoki/array.h>

#include <cassert>
#include <vector>

namespace lux2
{

    struct BsdfPtrTable
    {
        // Indexed by material id.
        std::vector<const BSDF *> ptrs;

        // Gather a BSDFPtr for the given material ids. Out of range = null.
        BSDFPtr Gather(const UInt32P &matID, MaskP active) const
        {
            const MaskP inRange =
                active && (matID < UInt32P(uint32_t(ptrs.size())));
            assert(!enoki::any(active && !inRange) &&
                   "BsdfPtrTable::Gather: material id out of range");
            return enoki::gather<BSDFPtr>(ptrs.data(), matID, inRange);
        }
    };

} // namespace lux2

#endif // LUX2_BSDFPTR_TABLE_H
