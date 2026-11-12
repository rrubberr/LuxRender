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

#ifndef LUX2_PIXELSAMPLERS_LOWDISCREPANCY_H
#define LUX2_PIXELSAMPLERS_LOWDISCREPANCY_H

#include "core/dynload.h"
#include "core/pixelsampler.h"

#include <cstdint>
#include <memory>

namespace lux2
{

    // Lowdiscrepancy scrambled Van Der Corput (x) and Sobol' (y) sequence.
    class LowDiscrepancyPixelSampler : public PixelSampler
    {
    public:
        LowDiscrepancyPixelSampler();

        std::unique_ptr<PixelSampler> Clone() const override
        {
            return std::make_unique<LowDiscrepancyPixelSampler>(*this);
        }

        uint32_t TotalPixels(const Tile &tile) const override
        {
            return uint32_t((tile.x1 - tile.x0) * (tile.y1 - tile.y0));
        }

        void GetPixel(uint32_t pos, const Tile &tile,
                      int *x, int *y) const override;

        static std::shared_ptr<PixelSampler> CreatePixelSampler(
            const PluginContext &ctx)
        {
            (void)ctx;
            return std::make_shared<LowDiscrepancyPixelSampler>();
        }

    private:
        uint32_t m_xSeed;
        uint32_t m_ySeed;
    };

} // namespace lux2

#endif // LUX2_PIXELSAMPLERS_LOWDISCREPANCY_H
