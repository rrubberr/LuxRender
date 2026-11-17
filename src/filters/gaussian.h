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

#ifndef LUX2_GAUSSIAN_H
#define LUX2_GAUSSIAN_H

#include "core/filter.h"

#include <cmath>
#include <memory>

namespace lux2
{

    struct PluginContext;

    class GaussianFilter : public Filter
    {
    public:
        GaussianFilter(float xw, float yw, float alpha)
            : m_xWidth(xw), m_yWidth(yw), m_alpha(alpha),
              m_expX(std::exp(-alpha * xw * xw)),
              m_expY(std::exp(-alpha * yw * yw)) {}

        float GetXWidth() const override { return m_xWidth; }
        float GetYWidth() const override { return m_yWidth; }

        // Separable 1D kernels.
        float EvaluateX(float dx) const override { return G(dx, m_expX); }
        float EvaluateY(float dy) const override { return G(dy, m_expY); }

        // Ealuate the windowed Gaussian directly on the lanes.
        FloatP EvaluateXP(const FloatP &dx) const override
        {
            return enoki::max(enoki::exp(-m_alpha * dx * dx) - m_expX,
                              FloatP(0.f));
        }
        FloatP EvaluateYP(const FloatP &dy) const override
        {
            return enoki::max(enoki::exp(-m_alpha * dy * dy) - m_expY,
                              FloatP(0.f));
        }

        static std::shared_ptr<Filter> CreateFilter(const PluginContext &ctx);

    private:
        float G(float d, float expv) const
        {
            return std::max(0.f, std::exp(-m_alpha * d * d) - expv);
        }

        float m_xWidth, m_yWidth, m_alpha, m_expX, m_expY;
    };

} // namespace lux2

#endif // LUX2_GAUSSIAN_H
