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

#include "pixelsamplers/hilbert.h"

#include "core/dynload.h"
#include "core/register.h"

namespace lux2
{

    namespace
    {
        uint32_t RoundUpPow2(uint32_t n)
        {
            uint32_t p = 1;
            while (p < n)
                p <<= 1;
            return p;
        }
    } // namespace

    void HilbertPixelSampler::HilberCurve(uint32_t n, int xo, int yo,
                                          int xd, int yd, int xp, int yp,
                                          int xEnd, int yEnd) const
    {
        if (n <= 1)
        {
            if ((xo <= xEnd) && (yo <= yEnd))
                m_curve.emplace_back(xo, yo);
            return;
        }
        const uint32_t n2 = n >> 1;
        HilberCurve(n2, xo, yo, xp, yp, xd, yd, xEnd, yEnd);
        HilberCurve(n2, xo + int(xd * n2), yo + int(yd * n2),
                    xd, yd, xp, yp, xEnd, yEnd);
        HilberCurve(n2, xo + int((xp + xd) * n2), yo + int((yp + yd) * n2),
                    xd, yd, xp, yp, xEnd, yEnd);
        HilberCurve(n2,
                    xo + int(xd * (n2 - 1)) + int(xp * (n - 1)),
                    yo + int(yd * (n2 - 1)) + int(yp * (n - 1)),
                    -xp, -yp, -xd, -yd, xEnd, yEnd);
    }

    void HilbertPixelSampler::EnsureBuilt(const Tile &tile) const
    {
        if (m_hasBuilt && tile.x0 == m_built.x0 && tile.x1 == m_built.x1 &&
            tile.y0 == m_built.y0 && tile.y1 == m_built.y1)
            return;

        const int xSize = tile.x1 - tile.x0;
        const int ySize = tile.y1 - tile.y0;
        uint32_t n = RoundUpPow2(uint32_t(std::max(xSize, ySize)));
        m_curve.clear();
        m_curve.reserve(size_t(xSize) * size_t(ySize));
        HilberCurve(n, tile.x0, tile.y0, 0, 1, 1, 0,
                    tile.x1 - 1, tile.y1 - 1);
        m_built = tile;
        m_hasBuilt = true;
    }

    LUX2_REGISTER_PIXELSAMPLER(HilbertPixelSampler, "hilbert");

} // namespace lux2
