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

#ifndef LUX2_CATMULLROM_H
#define LUX2_CATMULLROM_H

#include "core/filter.h"

#include <cmath>
#include <memory>

namespace lux2
{

    struct PluginContext;

    // Cubic Catmull-Rom kernel filter.
    class CatmullRomFilter : public Filter
    {
    public:
        CatmullRomFilter(float xw, float yw)
            : m_xWidth(xw), m_yWidth(yw) {}

        float GetXWidth() const override { return m_xWidth; }
        float GetYWidth() const override { return m_yWidth; }

        // Separable 1D kernels.
        float EvaluateX(float dx) const override { return K(dx); }
        float EvaluateY(float dy) const override { return K(dy); }

        FloatP EvaluateXP(const FloatP &dx) const override { return KP(dx); }
        FloatP EvaluateYP(const FloatP &dy) const override { return KP(dy); }

        static std::shared_ptr<Filter> CreateFilter(const PluginContext &ctx);

    private:
        static float K(float d)
        {
            const float x = std::abs(d);
            const float x2 = x * x;
            return (x >= 2.f) ? 0.f
                              : ((x < 1.f) ? (3.f * (x * x2) - 5.f * x2 + 2.f)
                                           : (-(x * x2) + 5.f * x2 - 8.f * x + 4.f));
        }

        static FloatP KP(const FloatP &d)
        {
            const FloatP x = enoki::abs(d);
            const FloatP x2 = x * x;
            const FloatP x3 = x2 * x;
            const FloatP inner = 3.f * x3 - 5.f * x2 + 2.f;
            const FloatP outer = -x3 + 5.f * x2 - 8.f * x + 4.f;
            return enoki::select(x >= 2.f, FloatP(0.f),
                                 enoki::select(x < 1.f, inner, outer));
        }

        float m_xWidth, m_yWidth;
    };

} // namespace lux2

#endif // LUX2_CATMULLROM_H
