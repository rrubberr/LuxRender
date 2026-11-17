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

#ifndef LUX2_TRIANGLE_H
#define LUX2_TRIANGLE_H

#include "core/filter.h"

#include <cmath>
#include <memory>

namespace lux2
{

    struct PluginContext;

    // Linear tent kernel, transcribed from legacy TriangleFilter::Evaluate.
    class TriangleFilter : public Filter
    {
    public:
        TriangleFilter(float xw, float yw)
            : m_xWidth(xw), m_yWidth(yw) {}

        float GetXWidth() const override { return m_xWidth; }
        float GetYWidth() const override { return m_yWidth; }

        // Separable 1D kernels.
        float EvaluateX(float dx) const override
        {
            return std::max(0.f, m_xWidth - std::abs(dx));
        }
        float EvaluateY(float dy) const override
        {
            return std::max(0.f, m_yWidth - std::abs(dy));
        }

        FloatP EvaluateXP(const FloatP &dx) const override
        {
            return enoki::max(FloatP(0.f), FloatP(m_xWidth) - enoki::abs(dx));
        }
        FloatP EvaluateYP(const FloatP &dy) const override
        {
            return enoki::max(FloatP(0.f), FloatP(m_yWidth) - enoki::abs(dy));
        }

        static std::shared_ptr<Filter> CreateFilter(const PluginContext &ctx);

    private:
        float m_xWidth, m_yWidth;
    };

} // namespace lux2

#endif // LUX2_TRIANGLE_H
