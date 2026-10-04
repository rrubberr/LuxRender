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

#include "samplers/lowdiscrepancy.h"

#include "core/dynload.h"
#include "core/paramset.h"
#include "core/qmc.h"
#include "core/register.h"

#include <enoki/array.h>

namespace lux2
{

    namespace
    {

        // Round up to the next square power of two.
        uint32_t RoundSquarePowerOfTwo(uint32_t count)
        {
            uint32_t res = 2;
            while (res * res < count)
            {
                // res = round_to_power_of_two(res + 1).
                uint32_t v = res + 1;
                uint32_t p = 2;
                while (p < v)
                    p <<= 1;
                res = p;
            }
            return res * res;
        }

    } // namespace

    LDSampler::LDSampler(uint32_t sampleCount, uint64_t baseSeed)
        : m_sampleCount(RoundSquarePowerOfTwo(sampleCount)),
          m_dimensionIndex(0),
          m_sampleIndex(0),
          m_baseSeed(baseSeed)
    {
        m_scrambleSeed = UInt32P(0u);
    }

    void LDSampler::Seed(uint64_t seedOffset, size_t wavefrontSize)
    {
        m_dimensionIndex = 0u;
        m_sampleIndex = 0u;
        (void)wavefrontSize;

        // Unique seed per (sequence, lane).
        const UInt32P laneIdx = enoki::arange<UInt32P>();
        const UInt32P seq = UInt32P(uint32_t(m_baseSeed & 0xffffffffu)) +
                            UInt32P(uint32_t(seedOffset));
        m_scrambleSeed = sample_tea_32(seq, laneIdx);
    }

    void LDSampler::Advance()
    {
        m_dimensionIndex = 0u;
        m_sampleIndex++;
    }

    FloatP LDSampler::Get1D(const UInt32P &seed, const UInt32P &sidx,
                            const UInt32P &dim, MaskP /*active*/) const
    {
        // Shuffle the sample order per dimension, then bit-reverse.
        const UInt32P permSeed = seed + dim;
        const UInt32P i = permute(sidx, m_sampleCount, permSeed);
        const UInt32P scramble = sample_tea_32(seed, UInt32P(0x48bc48ebu));
        return radical_inverse_2(i, scramble);
    }

    Point2fP LDSampler::Get2D(const UInt32P &seed, const UInt32P &sidx,
                              const UInt32P &dim, MaskP /*active*/) const
    {
        // One stream with two components via scramble.
        const UInt32P permSeed = seed + dim;
        const UInt32P i = permute(sidx, m_sampleCount, permSeed);
        const UInt32P scrambleX = sample_tea_32(seed, UInt32P(0x98bc51abu));
        const UInt32P scrambleY = sample_tea_32(seed, UInt32P(0x04223e2du));

        Point2fP result;
        result.x() = radical_inverse_2(i, scrambleX);
        result.y() = sobol_2(i, scrambleY);
        return result;
    }

    FloatP LDSampler::Next1D(MaskP active)
    {
        // Sequential view of current (scramble, sampleIndex, dimensionIndex).
        const UInt32P dim(m_dimensionIndex);
        FloatP v = Get1D(m_scrambleSeed, UInt32P(m_sampleIndex), dim, active);
        ++m_dimensionIndex;
        return v;
    }

    Point2fP LDSampler::Next2D(MaskP active)
    {
        const UInt32P dim(m_dimensionIndex);
        Point2fP v = Get2D(m_scrambleSeed, UInt32P(m_sampleIndex), dim, active);
        ++m_dimensionIndex;
        return v;
    }

    std::shared_ptr<Sampler> LDSampler::CreateSampler(const PluginContext &ctx)
    {
        // Accept either "count" (lux .lxs) or "spp".
        int count = ctx.params ? ctx.params->FindOneInt("count", 0) : 0;
        if (count <= 0 && ctx.params)
            count = ctx.params->FindOneInt("spp", 16);
        const int seed = ctx.params ? ctx.params->FindOneInt("seed", 0) : 0;
        return std::make_shared<LDSampler>(
            static_cast<uint32_t>(count), static_cast<uint64_t>(seed));
    }

    LUX2_REGISTER_SAMPLER(LDSampler, "lowdiscrepancy");

} // namespace lux2
