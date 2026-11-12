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

#include "core/math/vecp.h"

#include <cmath>
#include <vector>

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

        // Weight for each lane at its offset.
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

        // Pixel integrated 1D kernel g(t) = integral of h over [t-0.5, t+0.5],
        // sampled at integer offsets t in [-R, R] with R = ceil(width).
        std::vector<float> PixelIntegratedWeightsX() const
        {
            return PixelIntegrated(true);
        }
        std::vector<float> PixelIntegratedWeightsY() const
        {
            return PixelIntegrated(false);
        }

    private:
        // Composite Simpson quadrature of h over the unit box centered at t.
        std::vector<float> PixelIntegrated(bool xAxis) const
        {
            const float w = xAxis ? GetXWidth() : GetYWidth();
            const int R = int(std::ceil(w));
            const int taps = 2 * R + 1;
            std::vector<float> g(size_t(taps), 0.f);
            // Subsamples per unit box (odd count for Simpson).
            constexpr int N = 32;
            const float hstep = 1.f / float(N);
            for (int t = -R; t <= R; ++t)
            {
                // Integrate h(u) for u in [t-0.5, t+0.5].
                float sum = 0.f;
                for (int k = 0; k <= N; ++k)
                {
                    const float u = float(t) - 0.5f + hstep * float(k);
                    const float a = std::abs(u);
                    const float hv = xAxis ? EvaluateX(a) : EvaluateY(a);
                    // Simpson weights: 1,4,2,4,...,4,1.
                    float sw;
                    if (k == 0 || k == N)
                        sw = 1.f;
                    else if (k & 1)
                        sw = 4.f;
                    else
                        sw = 2.f;
                    sum += sw * hv;
                }
                g[size_t(t + R)] = sum * hstep / 3.f;
            }
            // Normalize to unit sum.
            float total = 0.f;
            for (float v : g)
                total += v;
            if (total != 0.f)
                for (float &v : g)
                    v /= total;
            return g;
        }
    };

} // namespace lux2

#endif // LUX2_FILTER_H
