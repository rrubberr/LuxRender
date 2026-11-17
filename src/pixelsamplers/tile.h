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

#ifndef LUX2_PIXELSAMPLERS_TILE_H
#define LUX2_PIXELSAMPLERS_TILE_H

#include "core/dynload.h"
#include "core/pixelsampler.h"

#include <algorithm>
#include <memory>

namespace lux2
{

    // Raster order within square blocks of the tile.
    class TilePixelSampler : public PixelSampler
    {
    public:
        explicit TilePixelSampler(int blockSize = 32)
            : m_blockSize(blockSize > 0 ? blockSize : 32)
        {
        }

        std::unique_ptr<PixelSampler> Clone() const override
        {
            return std::make_unique<TilePixelSampler>(*this);
        }

        uint32_t TotalPixels(const Tile &tile) const override
        {
            return uint32_t((tile.x1 - tile.x0) * (tile.y1 - tile.y0));
        }

        void GetPixel(uint32_t pos, const Tile &tile,
                      int *x, int *y) const override
        {
            const int w = tile.x1 - tile.x0;
            const int h = tile.y1 - tile.y0;
            const int b = m_blockSize;
            // Blocks are laid out row-major across the tile.
            const int bxCount = (w + b - 1) / b;
            int remaining = int(pos);
            for (int bi = 0;; ++bi)
            {
                const int bx = bi % bxCount, by = bi / bxCount;
                const int cw = std::min(b, w - bx * b);
                const int ch = std::min(b, h - by * b);
                if (remaining < cw * ch)
                {
                    *x = tile.x0 + bx * b + remaining % cw;
                    *y = tile.y0 + by * b + remaining / cw;
                    return;
                }
                remaining -= cw * ch;
            }
        }

        static std::shared_ptr<PixelSampler> CreatePixelSampler(
            const PluginContext &ctx);

    private:
        int m_blockSize;
    };

} // namespace lux2

#endif // LUX2_PIXELSAMPLERS_TILE_H
