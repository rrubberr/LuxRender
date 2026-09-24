/***************************************************************************
 *   Copyright (C) 1998-2026 by authors (see AUTHORS.txt)                  *
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

#ifndef LUX2_RNG_H
#define LUX2_RNG_H

#include "core/vecp.h"

#include <enoki/random.h>

namespace lux2
{

    // ---------------------------------------------------------------------------
    // RNGP
    // ---------------------------------------------------------------------------

    class RNGP
    {
    public:
        using FloatDist = enoki::PCG32<FloatP>;

        // Seed with a base state.
        explicit RNGP(UInt64 initstate = 0x853c49e6748fea9bULL)
            : gen(FloatDist(UInt64P(initstate))) {}

        // Uniform float in [0, 1), one value per lane.
        FloatP NextFloat() { return gen.template next_float<FloatP>(); }

        // Uniform float in [0, 1) for active lanes.
        FloatP NextFloat(const MaskP &mask) { return gen.template next_float<FloatP>(mask); }

        // Uniform integer in [0, n) per lane.
        UInt32P NextUInt32() { return gen.next_uint32(); }

        // Re-seed every lane from a scalar base state (e.g. per pixel).
        void Seed(UInt64 initstate)
        {
            gen.seed(UInt64P(initstate),
                     enoki::arange<UInt64P>() + 0xda3e39cb94b95bdbULL);
        }

    private:
        FloatDist gen;
    };

} // namespace lux2

#endif // LUX2_RNG_H
