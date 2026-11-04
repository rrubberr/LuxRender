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

// Compact finished lanes and emit to film.

#ifndef LUX2_INTEGRATORS_LANE_RETIRE_H
#define LUX2_INTEGRATORS_LANE_RETIRE_H

#include "core/color/color.h"
#include "core/film.h"
#include "core/math/vecp.h"

#include <enoki/array.h>

#include <cstddef>
#include <cstring>

namespace lux2
{

    // One retired sample.
    struct RetireRecord
    {
        FloatP x, y;
        FloatP alpha, camWeight;
        FloatP L; // SWCSpectrumP is FloatP
        FloatP w; // sw.w
    };

    // Batche retired samples so Push can compact with enoki::compress.
    class RetireBuffer
    {
    public:
        // compress() writes packet granularly with n <= 2W-1 pending
        // and up to W new entries.
        static constexpr size_t CAP = 3 * PACKET_WIDTH;

        void Push(const RetireRecord &r, const MaskP &finished)
        {
            float *px = bx + n, *py = by + n, *pL = bL + n;
            float *pwl = bwl + n, *pa = ba + n, *pwt = bwt + n;
            const size_t c = enoki::compress(px, r.x, finished);
            enoki::compress(py, r.y, finished);
            enoki::compress(pL, r.L, finished);
            enoki::compress(pwl, r.w, finished);
            enoki::compress(pa, r.alpha, finished);
            enoki::compress(pwt, r.camWeight, finished);
            n += c;
        }

        // Emit whole packets while they accumulate.
        void Flush(Film &dest, bool force)
        {
            while (n >= PACKET_WIDTH || (force && n > 0))
            {
                const size_t take =
                    n < PACKET_WIDTH ? n : size_t(PACKET_WIDTH);
                // Tail lanes contribute nothing.
                const MaskP real =
                    enoki::arange<FloatP>() < FloatP(float(take));
                auto loadMasked = [&](const float *b)
                {
                    return select(real, enoki::load_unaligned<FloatP>(b),
                                  FloatP(0.f));
                };
                SpectrumWavelengthsP sw;
                sw.FromWavelength(loadMasked(bwl));
                dest.Splat(loadMasked(bx), loadMasked(by),
                           SWCSpectrumP(loadMasked(bL)), sw,
                           loadMasked(ba), loadMasked(bwt));
                // Compact the remainder to the front.
                const size_t rem = n - take;
                std::memmove(bx, bx + take, rem * sizeof(float));
                std::memmove(by, by + take, rem * sizeof(float));
                std::memmove(bL, bL + take, rem * sizeof(float));
                std::memmove(bwl, bwl + take, rem * sizeof(float));
                std::memmove(ba, ba + take, rem * sizeof(float));
                std::memmove(bwt, bwt + take, rem * sizeof(float));
                n = rem;
            }
        }

    private:
        float bx[CAP], by[CAP], bL[CAP], bwl[CAP], ba[CAP], bwt[CAP];
        size_t n = 0;
    };
} // namespace lux2

#endif
