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

#ifndef LUX2_SCHEDULERPOLICY_H
#define LUX2_SCHEDULERPOLICY_H

#include <atomic>
#include <cstdint>

namespace lux2
{

    // The claimed unit of work is one pixel.
    struct PixelClaim
    {
        uint32_t pos;
    };

    // Flat pixel claim. Returns false once the cursor is exhausted.
    inline bool ClaimNext(std::atomic<uint32_t> &cursor, uint32_t total,
                          PixelClaim &out)
    {
        const uint32_t p = cursor.fetch_add(1, std::memory_order_relaxed);
        if (p >= total)
            return false;
        out.pos = p;
        return true;
    }

} // namespace lux2

#endif // LUX2_SCHEDULERPOLICY_H
