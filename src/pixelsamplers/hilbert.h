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

#ifndef LUX2_PIXELSAMPLERS_HILBERT_H
#define LUX2_PIXELSAMPLERS_HILBERT_H

#include "core/dynload.h"
#include "core/pixelsampler.h"

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace lux2
{

    // Space filling Hilbert curve order.
    class HilbertPixelSampler : public PixelSampler
    {
    public:
        HilbertPixelSampler() = default;

        std::unique_ptr<PixelSampler> Clone() const override
        {
            return std::make_unique<HilbertPixelSampler>(*this);
        }

        uint32_t TotalPixels(const Tile &tile) const override
        {
            return uint32_t((tile.x1 - tile.x0) * (tile.y1 - tile.y0));
        }

        void GetPixel(uint32_t pos, const Tile &tile,
                      int *x, int *y) const override
        {
            EnsureBuilt(tile);
            if (pos < m_curve.size())
            {
                *x = m_curve[pos].first;
                *y = m_curve[pos].second;
            }
            else
            {
                *x = tile.x0;
                *y = tile.y0;
            }
        }

        static std::shared_ptr<PixelSampler> CreatePixelSampler(
            const PluginContext &ctx)
        {
            (void)ctx;
            return std::make_shared<HilbertPixelSampler>();
        }

    private:
        void EnsureBuilt(const Tile &tile) const;
        void HilberCurve(uint32_t n, int xo, int yo, int xd, int yd,
                         int xp, int yp, int xEnd, int yEnd) const;

        mutable Tile m_built{0, 0, 0, 0};
        mutable bool m_hasBuilt = false;
        mutable std::vector<std::pair<int, int>> m_curve;
    };

} // namespace lux2

#endif // LUX2_PIXELSAMPLERS_HILBERT_H
