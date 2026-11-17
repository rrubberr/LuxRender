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

#ifndef LUX2_BLACKMANHARRIS_H
#define LUX2_BLACKMANHARRIS_H

#include "core/filter.h"

#include <cmath>
#include <memory>

namespace lux2
{

    struct PluginContext;

    // 4-term Blackman-Harris window.
    class BlackmanHarrisFilter : public Filter
    {
    public:
        BlackmanHarrisFilter(float xw, float yw)
            : m_xWidth(xw), m_yWidth(yw),
              m_xri(2.f / xw), m_yri(2.f / yw) {}

        float GetXWidth() const override { return m_xWidth; }
        float GetYWidth() const override { return m_yWidth; }

        // Separable 1D kernels.
        float EvaluateX(float dx) const override { return K(dx * m_xri); }
        float EvaluateY(float dy) const override { return K(dy * m_yri); }

        FloatP EvaluateXP(const FloatP &dx) const override
        {
            return KP(dx * FloatP(m_xri));
        }
        FloatP EvaluateYP(const FloatP &dy) const override
        {
            return KP(dy * FloatP(m_yri));
        }

        static std::shared_ptr<Filter> CreateFilter(const PluginContext &ctx);

    private:
        static float K(float x)
        {
            if (x < -1.f || x > 1.f)
                return 0.f;
            const float t = (x + 1.f) * 0.5f * float(M_PI);
            constexpr float A0 = 0.35875f;
            constexpr float A1 = -0.48829f;
            constexpr float A2 = 0.14128f;
            constexpr float A3 = -0.01168f;
            return A0 + A1 * std::cos(2.f * t) + A2 * std::cos(4.f * t) +
                   A3 * std::cos(6.f * t);
        }

        static FloatP KP(const FloatP &x)
        {
            const FloatP t = (x + FloatP(1.f)) * FloatP(0.5f) * FloatP(M_PI);
            constexpr float A0 = 0.35875f;
            constexpr float A1 = -0.48829f;
            constexpr float A2 = 0.14128f;
            constexpr float A3 = -0.01168f;
            const FloatP w = A0 + A1 * enoki::cos(2.f * t) +
                             A2 * enoki::cos(4.f * t) +
                             A3 * enoki::cos(6.f * t);
            return enoki::select((x < -1.f) || (x > 1.f), FloatP(0.f), w);
        }

        float m_xWidth, m_yWidth, m_xri, m_yri;
    };

} // namespace lux2

#endif // LUX2_BLACKMANHARRIS_H
