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

#ifndef LUX2_TONEMAPS_LINEARTONEMAP_H
#define LUX2_TONEMAPS_LINEARTONEMAP_H

#include "core/tonemap.h"

#include <cmath>

namespace lux2
{

    // Auto-linear (EV) tonemap.
    class EVOp : public ToneMap
    {
    public:
        explicit EVOp(float gamma) : m_gamma(gamma) {}
        virtual ~EVOp() {}

        virtual void Map(std::vector<XYZColor> &xyz,
                         int xRes, int yRes, float maxDisplayY) const;

        static std::unique_ptr<ToneMap> CreateToneMap(const ParamSet &ps);

    private:
        const float m_gamma;
    };

    // Linear tonemap using fixed exposure factor.
    class LinearOp : public ToneMap
    {
    public:
        LinearOp(float sensitivity, float exposure, float fstop, float gamma)
            : factor(exposure / (fstop * fstop) * sensitivity * 0.65f / 10.f *
                     std::pow(118.f / 255.f, gamma))
        {
        }
        virtual ~LinearOp() {}
        virtual void Map(std::vector<XYZColor> &xyz,
                         int xRes, int yRes, float maxDisplayY) const;

        static std::unique_ptr<ToneMap> CreateToneMap(const ParamSet &ps);

    private:
        const float factor;
    };

} // namespace lux2

#endif // LUX2_TONEMAPS_LINEARTONEMAP_H
