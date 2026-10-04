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

#ifndef LUX2_SAMPLER_H
#define LUX2_SAMPLER_H

#include "core/vecp.h"
#include "core/geometry.h"

#include <cstdint>
#include <memory>

namespace lux2
{

    // Produces quasirandom streams consumed by the integrator.
    class Sampler
    {
    public:
        virtual ~Sampler() = default;

        // A sampler instance must not be shared between workers.
        virtual std::unique_ptr<Sampler> Clone() const = 0;

        // Correlated samplers (e.g. Metropolis) are pinned to a fixed image region.
        virtual bool IsCorrelated() const { return false; }

        // Correlated samplers route each contribution through feedback.
        virtual bool RequiresFeedback() const { return false; }

        // Renderer configured base seed.
        virtual uint64_t BaseSeed() const { return 0; }

        // Seed for a wavefront of `wavefrontSize` lanes.
        virtual void Seed(uint64_t seedOffset, size_t wavefrontSize) = 0;

        // Advance to the next sample in the sequence. A subsequent Next1D/Next2D
        // returns the first component(s) of that sample.
        virtual void Advance() = 0;

        // Next 1D / 2D component of the current sample.
        virtual FloatP Next1D(MaskP active = MaskP(true)) = 0;
        virtual Point2fP Next2D(MaskP active = MaskP(true)) = 0;

        // Random access into a sequence independent of cursor state.
        // Used for lane compaction.
        virtual FloatP Get1D(const UInt32P &seed, const UInt32P &sidx,
                             const UInt32P &dim,
                             MaskP active = MaskP(true)) const = 0;
        virtual Point2fP Get2D(const UInt32P &seed, const UInt32P &sidx,
                               const UInt32P &dim,
                               MaskP active = MaskP(true)) const = 0;

        // Samples per pixel.
        virtual uint32_t SampleCount() const = 0;
    };

} // namespace lux2

#endif // LUX2_SAMPLER_H
