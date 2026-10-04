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

#ifndef LUX2_LOWDISCREPANCY_H
#define LUX2_LOWDISCREPANCY_H

#include "core/sampler.h"
#include "core/vecp.h"

#include <cstdint>
#include <memory>

namespace lux2
{

    struct PluginContext;

    class LDSampler : public Sampler
    {
    public:
        // spp is rounded up to a square power of two.
        LDSampler(uint32_t sampleCount, uint64_t baseSeed);

        std::unique_ptr<Sampler> Clone() const override
        {
            return std::make_unique<LDSampler>(*this);
        }

        uint64_t BaseSeed() const override { return m_baseSeed; }

        void Seed(uint64_t seedOffset, size_t wavefrontSize) override;
        void Advance() override;
        FloatP Next1D(MaskP active = MaskP(true)) override;
        Point2fP Next2D(MaskP active = MaskP(true)) override;
        FloatP Get1D(const UInt32P &seed, const UInt32P &sidx,
                     const UInt32P &dim,
                     MaskP active = MaskP(true)) const override;
        Point2fP Get2D(const UInt32P &seed, const UInt32P &sidx,
                       const UInt32P &dim,
                       MaskP active = MaskP(true)) const override;
        uint32_t SampleCount() const override { return m_sampleCount; }

        static std::shared_ptr<Sampler> CreateSampler(const PluginContext &ctx);

    private:
        uint32_t m_sampleCount;    // res^2
        uint32_t m_dimensionIndex; // per-sample stream counter
        uint32_t m_sampleIndex;    // current sample within the sequence
        uint64_t m_baseSeed;
        UInt32P m_scrambleSeed; // per-lane scramble
    };

} // namespace lux2

#endif // LUX2_LOWDISCREPANCY_H
