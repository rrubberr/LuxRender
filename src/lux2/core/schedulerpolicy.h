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

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>

namespace lux2
{

    // A claimed unit of work.
    struct Claim
    {
        uint32_t tile;
        uint32_t begin;
        uint32_t count;
    };

    // Pick the tile with the fewest claimed samples.
    // Returns false when every tile has reached haltSpp.
    inline bool ClaimNext(std::vector<std::atomic<uint32_t>> &cursor,
                          int haltSpp, uint32_t chunk, uint32_t startHint,
                          Claim &out)
    {
        const uint32_t n = uint32_t(cursor.size());
        for (;;)
        {
            uint32_t best = 0, bestV = UINT32_MAX;
            for (uint32_t i = 0; i < n; ++i) // tiles are few; a scan is cheap
            {
                const uint32_t t = (i + startHint) % n;
                const uint32_t v = cursor[t].load(std::memory_order_relaxed);
                if (v < bestV)
                {
                    bestV = v;
                    best = t;
                }
            }
            if (haltSpp > 0 && bestV >= uint32_t(haltSpp))
                return false;

            const uint32_t c = haltSpp > 0
                                   ? std::min(chunk, uint32_t(haltSpp) - bestV)
                                   : chunk;
            uint32_t expected = bestV;
            if (cursor[best].compare_exchange_weak(expected, bestV + c,
                                                   std::memory_order_relaxed))
            {
                out = Claim{best, bestV, c};
                return true;
            }
            // Lost the race for that tile; rescan and try again.
        }
    }

} // namespace lux2

#endif // LUX2_SCHEDULERPOLICY_H
