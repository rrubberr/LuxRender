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

#ifndef LUX2_PIXELSAMPLER_H
#define LUX2_PIXELSAMPLER_H

#include "core/tilequeue.h"

#include <cstdint>
#include <memory>

namespace lux2
{

    // Chooses which pixel a given visit position maps to.
    class PixelSampler
    {
    public:
        virtual ~PixelSampler() = default;

        // A pixel sampler instance must not be shared between workers.
        virtual std::unique_ptr<PixelSampler> Clone() const = 0;

        // Number of distinct pixels this pattern visits in the tile.
        virtual uint32_t TotalPixels(const Tile &tile) const = 0;

        // Pixel at visit position.
        virtual void GetPixel(uint32_t pos, const Tile &tile,
                              int *x, int *y) const = 0;
    };

} // namespace lux2

#endif // LUX2_PIXELSAMPLER_H
