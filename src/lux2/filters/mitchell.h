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

#ifndef LUX2_MITCHELL_H
#define LUX2_MITCHELL_H

#include "core/filter.h"

#include <cmath>
#include <memory>

namespace lux2
{

    struct PluginContext;

    // Mitchell-Netravali cubic.
    class MitchellFilter : public Filter
    {
    public:
        MitchellFilter(float xw, float yw, float b, float c)
            : m_xWidth(xw), m_yWidth(yw), m_B(b), m_C(c),
              m_invXWidth(1.f / xw), m_invYWidth(1.f / yw) {}

        float GetXWidth() const override { return m_xWidth; }
        float GetYWidth() const override { return m_yWidth; }

        float GetB() const { return m_B; }
        float GetC() const { return m_C; }

        // Separable 1D kernels.
        float EvaluateX(float dx) const override
        {
            return K(std::abs(dx) * m_invXWidth);
        }
        float EvaluateY(float dy) const override
        {
            return K(std::abs(dy) * m_invYWidth);
        }

        FloatP EvaluateXP(const FloatP &dx) const override
        {
            return KP(enoki::abs(dx) * FloatP(m_invXWidth));
        }
        FloatP EvaluateYP(const FloatP &dy) const override
        {
            return KP(enoki::abs(dy) * FloatP(m_invYWidth));
        }

        static std::shared_ptr<Filter> CreateFilter(const PluginContext &ctx);

    private:
        // x is the non-normalized non-negative offset.
        float K(float x) const
        {
            if (x >= 1.f)
                return 0.f;
            x = 2.f * x;
            if (x > 1.f)
                return (((-m_B / 6.f - m_C) * x + (m_B + 5.f * m_C)) * x +
                        (-2.f * m_B - 8.f * m_C)) *
                           x +
                       (4.f / 3.f * m_B + 4.f * m_C);
            return ((2.f - 1.5f * m_B - m_C) * x +
                    (-3.f + 2.f * m_B + m_C)) *
                       x * x +
                   (1.f - m_B / 3.f);
        }

        FloatP KP(const FloatP &x0) const
        {
            const FloatP x = 2.f * x0;
            const FloatP outer = (((-m_B / 6.f - m_C) * x +
                                   (m_B + 5.f * m_C)) *
                                      x +
                                  (-2.f * m_B - 8.f * m_C)) *
                                     x +
                                 (4.f / 3.f * m_B + 4.f * m_C);
            const FloatP inner = ((2.f - 1.5f * m_B - m_C) * x +
                                  (-3.f + 2.f * m_B + m_C)) *
                                     x * x +
                                 (1.f - m_B / 3.f);
            return enoki::select(x0 >= 1.f, FloatP(0.f),
                                 enoki::select(x > 1.f, outer, inner));
        }

        float m_xWidth, m_yWidth, m_B, m_C, m_invXWidth, m_invYWidth;
    };

} // namespace lux2

#endif // LUX2_MITCHELL_H
