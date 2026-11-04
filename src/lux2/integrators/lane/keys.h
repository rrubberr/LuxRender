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

#ifndef LUX2_INTEGRATORS_LANE_KEYS_H
#define LUX2_INTEGRATORS_LANE_KEYS_H

#include "core/math/vecp.h"

namespace lux2
{

    // Upper bound on path depth, for sizing RNG stride.
    constexpr int MAX_DEPTH = 128;

    // RNG dimension namespaces for the `all` strategies.
    constexpr uint32_t NEE_ALL_DIM = 1u << 20;
    constexpr uint32_t LIGHT_ALL_DIM = 1u << 22;

    // Connection RNG key for a light subpath slot and vertex counts.
    inline UInt32P ConnKey(const UInt32P &lightSlot, const Int32P &nE,
                           const Int32P &nL)
    {
        constexpr uint32_t S = uint32_t(MAX_DEPTH) + 1u;
        return (lightSlot * UInt32P(S) + UInt32P(nE)) * UInt32P(S) +
               UInt32P(nL);
    }

} // namespace lux2

#endif // LUX2_INTEGRATORS_LANE_KEYS_H
