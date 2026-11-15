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

// From the Cline et. al "Proposed mutations" convolution filter. BIASED.

// This is a plain scalar grid + blur + apply so can apply to any integrator
// with pixel visit count (MLT, SPPM, etc.).

#ifndef LUX2_INTEGRATORS_ERPT_SMOOTH_H
#define LUX2_INTEGRATORS_ERPT_SMOOTH_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace lux2
{
    namespace erpt
    {
        struct FilterGrid
        {
            int w = 0, h = 0;
            std::vector<float> v;
            void Init(int w_, int h_)
            {
                w = w_;
                h = h_;
                v.assign(size_t(w) * size_t(h), 0.f);
            }
            void Add(int x, int y, float c)
            {
                if (x >= 0 && y >= 0 && x < w && y < h)
                    v[size_t(y) * w + x] += c;
            }
        };

        // Separable box blur of radius r (window 2r+1) with edge
        // renormalisation.
        inline void BoxBlur(const FilterGrid &src, FilterGrid &dst, int r)
        {
            dst.Init(src.w, src.h);
            if (r <= 0)
            {
                dst.v = src.v;
                return;
            }
            std::vector<float> tmp(src.v.size(), 0.f);
            for (int y = 0; y < src.h; ++y)
            {
                // prefix sums per row
                std::vector<double> ps(size_t(src.w) + 1u, 0.0);
                for (int x = 0; x < src.w; ++x)
                    ps[size_t(x) + 1u] = ps[size_t(x)] + src.v[size_t(y) * src.w + x];
                for (int x = 0; x < src.w; ++x)
                {
                    const int x0 = std::max(0, x - r), x1 = std::min(src.w - 1, x + r);
                    tmp[size_t(y) * src.w + x] =
                        float((ps[size_t(x1) + 1u] - ps[size_t(x0)]) / double(x1 - x0 + 1));
                }
            }
            for (int x = 0; x < src.w; ++x)
            {
                std::vector<double> ps(size_t(src.h) + 1u, 0.0);
                for (int y = 0; y < src.h; ++y)
                    ps[size_t(y) + 1u] = ps[size_t(y)] + tmp[size_t(y) * src.w + x];
                for (int y = 0; y < src.h; ++y)
                {
                    const int y0 = std::max(0, y - r), y1 = std::min(src.h - 1, y + r);
                    dst.v[size_t(y) * src.w + x] =
                        float((ps[size_t(y1) + 1u] - ps[size_t(y0)]) / double(y1 - y0 + 1));
                }
            }
        }

        // No proposals -> leave the pixel alone. Scale is
        // clamped so fireflies cannot be amplified.
        inline float SmoothScale(float actual, float expected, float maxScale)
        {
            if (!(actual > 0.f) || !(expected > 0.f))
                return 1.f;
            const float s = expected / actual;
            return s > maxScale ? maxScale : s;
        }

        // Rescale `pix` (interleaved, `stride` floats per pixel, first
        // `nChan` channels) in place.
        inline void ApplyProposalFilter(float *pix, int stride, int nChan,
                                        const FilterGrid &actual, int radius,
                                        float maxScale = 16.f)
        {
            FilterGrid expected;
            BoxBlur(actual, expected, radius);
            const size_t n = actual.v.size();
            for (size_t i = 0; i < n; ++i)
            {
                const float s = SmoothScale(actual.v[i], expected.v[i], maxScale);
                for (int c = 0; c < nChan; ++c)
                    pix[i * size_t(stride) + size_t(c)] *= s;
            }
        }

    } // namespace erpt
} // namespace lux2

#endif // LUX2_INTEGRATORS_ERPT_SMOOTH_H
