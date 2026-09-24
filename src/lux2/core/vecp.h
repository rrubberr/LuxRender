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

#ifndef LUX2_VECP_H
#define LUX2_VECP_H

#include <cstddef>
#include <cstdint>

#include <enoki/array.h>

namespace lux2
{

// Number of lanes in an SoA packet = the SIMD width of the target.
#if defined(__AVX512F__)
    constexpr size_t PACKET_WIDTH = 16;
#elif defined(__AVX2__)
    constexpr size_t PACKET_WIDTH = 8;
#elif defined(__SSE4_2__)
    constexpr size_t PACKET_WIDTH = 4;
#else
    constexpr size_t PACKET_WIDTH = 1;
#endif

    // Scalar element types.
    using Float = float;
    using Int = std::int32_t;
    using UInt = std::uint32_t;

    // Packet types.
    using FloatP = enoki::Packet<Float, PACKET_WIDTH>;
    using Int32P = enoki::Packet<Int, PACKET_WIDTH>;
    using UInt32P = enoki::Packet<UInt, PACKET_WIDTH>;
    using MaskP = enoki::mask_t<FloatP>;

    // 64-bit lanes are used for PCG32 RNG state.
    using UInt64 = std::uint64_t;
    using UInt64P = enoki::Packet<UInt64, PACKET_WIDTH>;

} // namespace lux2

#endif // LUX2_VECP_H
