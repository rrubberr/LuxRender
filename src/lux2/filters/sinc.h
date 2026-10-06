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

#ifndef LUX2_SINC_H
#define LUX2_SINC_H

#include "core/filter.h"

#include <cmath>
#include <memory>

namespace lux2
{

    struct PluginContext;

    // Lanczos-sinc window.
    class LanczosSincFilter : public Filter
    {
    public:
        LanczosSincFilter(float xw, float yw, float t)
            : m_xWidth(xw), m_yWidth(yw),
              m_invXWidth(1.f / xw), m_invYWidth(1.f / yw), m_tau(t) {}

        float GetXWidth() const override { return m_xWidth; }
        float GetYWidth() const override { return m_yWidth; }

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
        // x is the normalized offset.
        float K(float x) const
        {
            if (x < 1e-5f)
                return 1.f;
            if (x > 1.f)
                return 0.f;
            const float t = x * float(M_PI);
            const float sinc = std::sin(t * m_tau) / (t * m_tau);
            const float lanczos = std::sin(t) / t;
            return sinc * lanczos;
        }

        FloatP KP(const FloatP &x) const
        {
            // Dead lanes must not NaN.
            const FloatP t = x * FloatP(M_PI);
            const FloatP tt = enoki::max(t * m_tau, FloatP(1e-20f));
            const FloatP tl = enoki::max(t, FloatP(1e-20f));
            const FloatP w = enoki::sin(tt) / tt * (enoki::sin(tl) / tl);
            return enoki::select(x < 1e-5f, FloatP(1.f),
                                 enoki::select(x > 1.f, FloatP(0.f), w));
        }

        float m_xWidth, m_yWidth, m_invXWidth, m_invYWidth, m_tau;
    };

} // namespace lux2

#endif // LUX2_SINC_H
