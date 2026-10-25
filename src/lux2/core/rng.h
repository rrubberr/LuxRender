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

#include "core/math/vecp.h"

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

        // Seed with a scalar base state.
        explicit RNGP(UInt64 initstate = 0x853c49e6748fea9bULL)
            : gen(FloatDist(UInt64P(initstate))) {}

        // Seed every lane from a (state, seq) pair.
        RNGP(const UInt64P &initstate, const UInt64P &initseq)
            : gen(initstate, initseq) {}

        // Uniform float in [0, 1), one value per lane.
        FloatP NextFloat() { return gen.template next_float<FloatP>(); }

        // Uniform float in [0, 1) for active lanes.
        FloatP NextFloat(const MaskP & /*mask*/) { return gen.template next_float<FloatP>(); }

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
    // BDPT stream salts.
    constexpr UInt64 BDPT_EYE_SALT = 0x5A82799949075BULL;
    constexpr UInt64 BDPT_LIGHT_SALT = 0x3C374B077C86EA69ULL;
    constexpr UInt64 BDPT_CONN_SALT = 0x7F4A7C159E3779B9ULL;

    // Fibonacci hashing multipliers.
    constexpr UInt64 MIX_GOLDEN = 0x9E3779B97F4A7C15ULL;  // splitmix64 additive step
    constexpr UInt64 MIX_MUL1 = 0xBF58476D1CE4E5B9ULL;    // splitmix64 multiplier 1
    constexpr UInt64 MIX_MUL2 = 0x94D049BB133111EBULL;    // splitmix64 multiplier 2
    constexpr UInt64 MIX_MUL3 = 0xD6E8FEB86659FD93ULL;    // splitmix64 multiplier 3
    constexpr UInt64 MIX_SEQ_XOR = 0xDA3E39CB94B95BDBULL; // sequence stream offset
    constexpr UInt64 MIX_SEQ_MUL = 0x2545F4914F6CDD1DULL; // sequence mix multiplier

    // (state, seq) pair handed to PCG32 to key a stream.
    struct StreamKeyResult
    {
        UInt64P state;
        UInt64P seq;
    };

    // Fold the addressing tuple into an independent (state, seq) pair per lane.
    inline StreamKeyResult StreamKey(const UInt32P &seed, const UInt32P &sampleIdx,
                                     const Int32P &bounce, const Int32P &depth,
                                     const UInt32P &lightIdx, UInt64 salt)
    {
        // Broadcast the shared mix constants across the wavefront.
        const UInt64P M1(MIX_GOLDEN);
        const UInt64P M2(MIX_MUL1);
        const UInt64P M3(MIX_MUL2);
        const UInt64P M4(MIX_MUL3);
        const UInt64P M5(MIX_SEQ_XOR);
        const UInt64P M6(MIX_SEQ_MUL);

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
        // A second mix for the sequence so state and seq are not related.
        UInt64P s2 = h ^ M5;
        s2 ^= s2 >> 32;
        s2 *= M6;
        s2 ^= s2 >> 28;
        return StreamKeyResult{h, s2};
    }

    // Construct an RNGP already keyed to an independent stream.
    inline RNGP KeyedRNG(const UInt32P &seed, const UInt32P &sampleIdx,
                         const Int32P &bounce, const Int32P &depth,
                         const UInt32P &lightIdx, UInt64 salt)
    {
        const StreamKeyResult k = StreamKey(seed, sampleIdx, bounce, depth,
                                            lightIdx, salt);
        return RNGP(k.state, k.seq);
    }

    // ---------------------------------------------------------------------------
    // Scalar seed derivation
    // ---------------------------------------------------------------------------

    // 64-bit mixer.
    inline uint64_t SplitMix64(uint64_t x)
    {
        x += MIX_GOLDEN;
        x = (x ^ (x >> 30)) * MIX_MUL1;
        x = (x ^ (x >> 27)) * MIX_MUL2;
        return x ^ (x >> 31);
    }

    // Correlated mode additive salt per render round.
    inline uint64_t RoundSeed(uint64_t baseSeed, int round)
    {
        return baseSeed + uint64_t(uint32_t(round)) * MIX_GOLDEN;
    }

    // Decorrelated mode distinct seed per (round, group).
    inline uint64_t GroupPassSeed(uint64_t baseSeed, int round, int group)
    {
        return SplitMix64(RoundSeed(baseSeed, round) ^ uint64_t(uint32_t(group)));
    }

    // Pixel seed fold shared by integrator lane refill.
    inline uint64_t PixelSeed(uint64_t sampleBasis, uint64_t baseSeed,
                              int px, int py)
    {
        const uint64_t h =
            ((sampleBasis ^ baseSeed) * MIX_GOLDEN) ^
            (uint64_t(px) * MIX_MUL1) ^
            (uint64_t(py) * MIX_MUL2);
        return h;
    }

} // namespace lux2

#endif // LUX2_RNG_H
