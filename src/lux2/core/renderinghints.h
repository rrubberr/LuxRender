/***************************************************************************
 *   Copyright (C) 1998-2026 by authors (see AUTHORS.txt)                  *
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

#ifndef RENDERINGHINTS_H
#define RENDERINGHINTS_H

#include "core/vecp.h"
#include <cstdint>
#include <string>
#include <vector>

namespace lux2
{

    class Light;
    class Scene;

    // pSel/cdf/task are indexed by group-light index k.
    struct LightsSamplingStrategy
    {
        enum class StrategyKind
        {
            Uniform,
            Cdf,
            All
        } kind = StrategyKind::Uniform;

        std::vector<float> pSel;     // expected NEE samples of light k per vertex
        std::vector<float> cdf;      // StrategyKind::Cdf, inclusive, length N+1, cdf[0]==0, cdf[N]==1
        std::vector<uint32_t> task;  // StrategyKind::All, light index k, repeated n_k times
        std::vector<float> areaPSel; // by area-light id a, 0 if not in groupId

        uint32_t TaskCount() const { return kind == StrategyKind::All ? (uint32_t)task.size() : 1u; }
    };

    // Parse `name` and build tables for lights in `group`.
    LightsSamplingStrategy CreateLightsSamplingStrategy(
        const std::string &name,
        const std::vector<const Light *> &group,
        int groupId,
        const Scene &scene);

    // CDF inversion.
    UInt32P SearchSortedCDF(const std::vector<float> &cdf, const FloatP &u,
                            const MaskP &active);

    // Selection of one light for NEE with Uniform or Cdf strategy.
    struct LightPick
    {
        UInt32P idx;
        FloatP pSel;
    };

    LightPick SampleLight(const LightsSamplingStrategy &ls, const FloatP &uLight,
                          const MaskP &active);

} // namespace lux2

#endif // RENDERINGHINTS_H
