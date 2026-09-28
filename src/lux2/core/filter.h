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

#ifndef LUX2_FILTER_H
#define LUX2_FILTER_H

#include "core/vecp.h"

namespace lux2
{

    // Abstract pixel reconstruction filter.
    class Filter
    {
    public:
        virtual ~Filter() = default;

        // Filter footprint half-widths in pixel units.
        virtual float GetXWidth() const = 0;
        virtual float GetYWidth() const = 0;

        // Separable 1D kernels at an offset from the pixel center.
        virtual float EvaluateX(float dx) const = 0;
        virtual float EvaluateY(float dy) const = 0;

        // Filter weight at offset from the pixel center.
        virtual float Evaluate(float dx, float dy) const
        {
            return EvaluateX(dx) * EvaluateY(dy);
        }

        // Weight for each lane at its own offset.
        virtual FloatP EvaluateXP(const FloatP &dx) const
        {
            float in[PACKET_WIDTH], t[PACKET_WIDTH];
            enoki::store_unaligned(in, dx);
            for (size_t i = 0; i < PACKET_WIDTH; ++i)
                t[i] = EvaluateX(in[i]);
            return enoki::load_unaligned<FloatP>(t);
        }
        virtual FloatP EvaluateYP(const FloatP &dy) const
        {
            float in[PACKET_WIDTH], t[PACKET_WIDTH];
            enoki::store_unaligned(in, dy);
            for (size_t i = 0; i < PACKET_WIDTH; ++i)
                t[i] = EvaluateY(in[i]);
            return enoki::load_unaligned<FloatP>(t);
        }
    };

} // namespace lux2

#endif // LUX2_FILTER_H
