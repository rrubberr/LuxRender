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

#ifndef LUX2_MEDIUMPTR_TABLE_H
#define LUX2_MEDIUMPTR_TABLE_H

#include "core/medium.h"
#include "core/medium_call.h"

#include <enoki/array.h>

#include <cassert>
#include <vector>

namespace lux2
{

    // Registry of medium pointers indexed by medium id.
    struct MediumPtrTable
    {
        std::vector<const Medium *> ptrs;

        // Gather a MediumPtr for the given medium ids. Out of range ids
        // clamp to clear.
        MediumPtr Gather(const UInt32P &mediumID, MaskP active) const
        {
            const UInt32P clamped = select(
                mediumID >= UInt32P(uint32_t(ptrs.size())),
                UInt32P(LUX2_MEDIUM_CLEAR),
                mediumID);
            return enoki::gather<MediumPtr>(ptrs.data(), clamped, active);
        }

        // True if any registered medium is not clear.
        bool HasMedia() const
        {
            for (size_t i = 1; i < ptrs.size(); ++i)
                if (ptrs[i] != nullptr)
                    return true;
            return false;
        }
    };

} // namespace lux2

#endif // LUX2_MEDIUMPTR_TABLE_H
