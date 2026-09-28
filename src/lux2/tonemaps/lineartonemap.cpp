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

#include "core/paramset.h"

#include "tonemaps/lineartonemap.h"

namespace lux2
{

    // EVOp Method Definitions.
    void EVOp::Map(std::vector<XYZColor> &xyz,
                   int xRes, int yRes, float maxDisplayY) const
    {
        (void)maxDisplayY;

        const int numPixels = xRes * yRes;

        float Y = 0.f;
        int nPixels = 0;
        for (int i = 0; i < numPixels; ++i)
        {
            if (xyz[i].Y() <= 0.f)
                continue;
            Y += xyz[i].Y();
            ++nPixels;
        }
        Y = Y / float(std::max(1, nPixels));

        if (Y <= 0.f)
            return;

        const float factor = (1.25f / Y) * std::pow(118.f / 255.f, m_gamma);

        for (int i = 0; i < numPixels; ++i)
            xyz[i] *= factor;
    }

    std::unique_ptr<ToneMap> EVOp::CreateToneMap(const ParamSet &ps)
    {
        const float gamma = ps.FindOneFloat("gamma", 2.2f);
        return std::unique_ptr<ToneMap>(new EVOp(gamma));
    }

    // LinearOp Method Definitions.
    void LinearOp::Map(std::vector<XYZColor> &xyz,
                       int xRes, int yRes, float maxDisplayY) const
    {
        (void)maxDisplayY;

        const int numPixels = xRes * yRes;
        for (int i = 0; i < numPixels; ++i)
            xyz[i] *= factor;
    }

    std::unique_ptr<ToneMap> LinearOp::CreateToneMap(const ParamSet &ps)
    {
        const float sensitivity = ps.FindOneFloat("sensitivity", 100.f);
        const float exposure = ps.FindOneFloat("exposure", 1.f / 1000.f);
        const float fstop = ps.FindOneFloat("fstop", 2.8f);
        const float gamma = ps.FindOneFloat("gamma", 2.2f);
        return std::unique_ptr<ToneMap>(
            new LinearOp(sensitivity, exposure, fstop, gamma));
    }

    LUX2_REGISTER_TONEMAP(EVOp, "autolinear");
    LUX2_REGISTER_TONEMAP(LinearOp, "linear");

} // namespace lux2
