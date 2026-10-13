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

        // Lane seed.
        void Seed(const UInt64P &initstate, const UInt64P &initseq)
        {
            gen.seed(initstate, initseq);
        }

    private:
        FloatDist gen;
    };

    // ---------------------------------------------------------------------------
    // Stream keying
    // ---------------------------------------------------------------------------

    constexpr UInt64 FLIGHT_SALT = 0x9E3779B97F4A7C15ULL;
    constexpr UInt64 SHADOW_SALT = 0x2C9277B5D6B0A3E1ULL;

    // Combine the addressing tuple into a (state, seq) pair.
    inline void StreamKey(const UInt32P &seed, const UInt32P &sampleIdx,
                          const Int32P &bounce, const Int32P &depth,
                          const UInt32P &lightIdx, UInt64 salt,
                          UInt64P *state, UInt64P *seq)
    {
        // Fold the tuple into one 64-bit word per lane.
        const UInt64P M1(0x9E3779B97F4A7C15ULL);
        const UInt64P M2(0xBF58476D1CE4E5B9ULL);
        const UInt64P M3(0x94D049BB133111EBULL);
        const UInt64P M4(0xD6E8FEB86659FD93ULL);
        const UInt64P M5(0xDA3E39CB94B95BDBULL);
        const UInt64P M6(0x2545F4914F6CDD1DULL);

        UInt64P h = UInt64P(seed);
        h = h * M1 + UInt64P(sampleIdx);
        h = h * M2 + UInt64P(bounce);
        h = h * M3 + UInt64P(depth);
        h = h * M4 + UInt64P(lightIdx);
        h = h ^ UInt64P(salt);
        h ^= h >> 30;
        h *= M2;
        h ^= h >> 27;
        h *= M3;
        h ^= h >> 31;
        *state = h;
        // A second independent mix for the sequence so state and seq are not
        // related.
        UInt64P s2 = h ^ M5;
        s2 ^= s2 >> 32;
        s2 *= M6;
        s2 ^= s2 >> 28;
        *seq = s2;
    }

} // namespace lux2

#endif // LUX2_RNG_H
