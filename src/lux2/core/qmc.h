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

#ifndef LUX2_QMC_H
#define LUX2_QMC_H

#include "core/vecp.h"

#include <enoki/array.h>

namespace lux2
{

    // Returns a uniformly distributed 32-bit integer per lane.
    inline UInt32P sample_tea_32(const UInt32P &v0_, const UInt32P &v1_,
                                 int rounds = 4)
    {
        UInt32P v0 = v0_, v1 = v1_;
        UInt32P sum(0u);
        for (int i = 0; i < rounds; ++i)
        {
            sum += UInt32P(0x9e3779b9u);
            v0 += (enoki::sl<4>(v1) + UInt32P(0xa341316cu)) ^
                  (v1 + sum) ^
                  (enoki::sr<5>(v1) + UInt32P(0xc8013ea4u));
            v1 += (enoki::sl<4>(v0) + UInt32P(0xad90777du)) ^
                  (v0 + sum) ^
                  (enoki::sr<5>(v0) + UInt32P(0x7e95761eu));
        }
        return v1;
    }

    // Pseudorandom permutation of `index` within [0, sample_count).
    inline UInt32P permute(const UInt32P &index_, uint32_t sample_count,
                           const UInt32P &seed, int rounds = 2)
    {
        UInt32P index = index_;
        uint32_t n = 0;
        while ((1u << n) < sample_count)
            ++n; // n = log2(sample_count)
        for (uint32_t level = 0; level < n; ++level)
        {
            const UInt32P bit(1u << level);
            // A random decision identical for the pair that may swap at this level.
            const UInt32P rand = sample_tea_32(index | bit, seed, rounds);
            enoki::masked(index, eq(rand & bit, bit)) = index ^ bit;
        }
        return index;
    }

    // Van der Corput radical inverse in base 2.
    inline FloatP radical_inverse_2(const UInt32P &index_,
                                    const UInt32P &scramble)
    {
        UInt32P index = index_;
        index = (index << 16) | (index >> 16);
        index = ((index & UInt32P(0x00ff00ffu)) << 8) |
                ((index & UInt32P(0xff00ff00u)) >> 8);
        index = ((index & UInt32P(0x0f0f0f0fu)) << 4) |
                ((index & UInt32P(0xf0f0f0f0u)) >> 4);
        index = ((index & UInt32P(0x33333333u)) << 2) |
                ((index & UInt32P(0xccccccccu)) >> 2);
        index = ((index & UInt32P(0x55555555u)) << 1) |
                ((index & UInt32P(0xaaaaaaaau)) >> 1);

        // Uniform single-precision value in [1,2) from the scrambled bits, minus 1.
        return enoki::reinterpret_array<FloatP>(
                   enoki::sr<9>(index ^ scramble) | UInt32P(0x3f800000u)) -
               FloatP(1.f);
    }

    // Sobol' radical inverse in base 2.
    inline FloatP sobol_2(const UInt32P &index_, UInt32P scramble)
    {
        UInt32P index = index_;
        UInt32P v(1u << 31);
        while (enoki::any(index != UInt32P(0u)))
        {
            enoki::masked(scramble, eq(index & UInt32P(1u), UInt32P(1u))) ^= v;
            index = enoki::sr<1>(index);
            v ^= enoki::sr<1>(v);
        }
        return scramble / FloatP(4294967296.f); // / 2^32
    }

} // namespace lux2

#endif // LUX2_QMC_H
