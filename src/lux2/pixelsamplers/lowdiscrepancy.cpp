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

#include "pixelsamplers/lowdiscrepancy.h"

#include "core/dynload.h"
#include "core/register.h"

#include <random>

namespace lux2
{

    namespace
    {
        // Radical inverse base 2 with XOR scramble.
        float VanDerCorput(uint32_t n, uint32_t scramble)
        {
            n = (n << 16) | (n >> 16);
            n = ((n & 0x00ff00ffu) << 8) | ((n & 0xff00ff00u) >> 8);
            n = ((n & 0x0f0f0f0fu) << 4) | ((n & 0xf0f0f0f0u) >> 4);
            n = ((n & 0x33333333u) << 2) | ((n & 0xccccccccu) >> 2);
            n = ((n & 0x55555555u) << 1) | ((n & 0xaaaaaaaau) >> 1);
            n ^= scramble;
            return float(double(n) / double(0x100000000LL));
        }

        // Sobol' base 2 dimension 2 with XOR scramble.
        float Sobol2(uint32_t n, uint32_t scramble)
        {
            for (uint32_t v = 1u << 31; n != 0; n >>= 1, v ^= v >> 1)
                if (n & 0x1u)
                    scramble ^= v;
            return float(double(scramble) / double(0x100000000LL));
        }

        uint32_t NextSeed()
        {
            thread_local std::mt19937 rng{std::random_device{}()};
            return rng();
        }
    } // namespace

    LowDiscrepancyPixelSampler::LowDiscrepancyPixelSampler()
        : m_xSeed(NextSeed()), m_ySeed(NextSeed())
    {
    }

    void LowDiscrepancyPixelSampler::GetPixel(uint32_t pos, const Tile &tile,
                                              int *x, int *y) const
    {
        const int w = tile.x1 - tile.x0;
        const int h = tile.y1 - tile.y0;
        int px = tile.x0 + int(VanDerCorput(pos, m_xSeed) * float(w));
        int py = tile.y0 + int(Sobol2(pos, m_ySeed) * float(h));
        // Clamp against the tile bounds for safety.
        if (px < tile.x0)
            px = tile.x0;
        else if (px >= tile.x1)
            px = tile.x1 - 1;
        if (py < tile.y0)
            py = tile.y0;
        else if (py >= tile.y1)
            py = tile.y1 - 1;
        *x = px;
        *y = py;
    }

    LUX2_REGISTER_PIXELSAMPLER(LowDiscrepancyPixelSampler, "lowdiscrepancy");

} // namespace lux2
