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

#ifndef LUX2_PIXELSAMPLERS_LINEAR_H
#define LUX2_PIXELSAMPLERS_LINEAR_H

#include "core/dynload.h"
#include "core/pixelsampler.h"

#include <memory>

namespace lux2
{

    // Scanline order within the tile.
    class LinearPixelSampler : public PixelSampler
    {
    public:
        LinearPixelSampler() = default;

        std::unique_ptr<PixelSampler> Clone() const override
        {
            return std::make_unique<LinearPixelSampler>(*this);
        }

        uint32_t TotalPixels(const Tile &tile) const override
        {
            return uint32_t((tile.x1 - tile.x0) * (tile.y1 - tile.y0));
        }

        void GetPixel(uint32_t pos, const Tile &tile,
                      int *x, int *y) const override
        {
            const int w = tile.x1 - tile.x0;
            *x = tile.x0 + int(pos) % w;
            *y = tile.y0 + int(pos) / w;
        }

        static std::shared_ptr<PixelSampler> CreatePixelSampler(
            const PluginContext &ctx)
        {
            (void)ctx;
            return std::make_shared<LinearPixelSampler>();
        }
    };

} // namespace lux2

#endif // LUX2_PIXELSAMPLERS_LINEAR_H
