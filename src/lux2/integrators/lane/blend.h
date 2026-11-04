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

// Lane struct blend.

#ifndef LUX2_INTEGRATORS_LANE_BLEND_H
#define LUX2_INTEGRATORS_LANE_BLEND_H

#include "core/color/color.h"
#include "core/math/vecp.h"

#include <enoki/array.h>

#include <utility>

namespace lux2
{ // Apply f to each name in a field list.
#define LUX2_FE_1(f, x) f(x)
#define LUX2_FE_2(f, x, ...) f(x) LUX2_FE_1(f, __VA_ARGS__)
#define LUX2_FE_3(f, x, ...) f(x) LUX2_FE_2(f, __VA_ARGS__)
#define LUX2_FE_4(f, x, ...) f(x) LUX2_FE_3(f, __VA_ARGS__)
#define LUX2_FE_5(f, x, ...) f(x) LUX2_FE_4(f, __VA_ARGS__)
#define LUX2_FE_6(f, x, ...) f(x) LUX2_FE_5(f, __VA_ARGS__)
#define LUX2_FE_7(f, x, ...) f(x) LUX2_FE_6(f, __VA_ARGS__)
#define LUX2_FE_8(f, x, ...) f(x) LUX2_FE_7(f, __VA_ARGS__)
#define LUX2_FE_9(f, x, ...) f(x) LUX2_FE_8(f, __VA_ARGS__)
#define LUX2_FE_10(f, x, ...) f(x) LUX2_FE_9(f, __VA_ARGS__)
#define LUX2_FE_11(f, x, ...) f(x) LUX2_FE_10(f, __VA_ARGS__)
#define LUX2_FE_12(f, x, ...) f(x) LUX2_FE_11(f, __VA_ARGS__)
#define LUX2_FE_13(f, x, ...) f(x) LUX2_FE_12(f, __VA_ARGS__)
#define LUX2_FE_14(f, x, ...) f(x) LUX2_FE_13(f, __VA_ARGS__)
#define LUX2_FE_15(f, x, ...) f(x) LUX2_FE_14(f, __VA_ARGS__)
#define LUX2_FE_16(f, x, ...) f(x) LUX2_FE_15(f, __VA_ARGS__)
#define LUX2_FE_17(f, x, ...) f(x) LUX2_FE_16(f, __VA_ARGS__)
#define LUX2_FE_18(f, x, ...) f(x) LUX2_FE_17(f, __VA_ARGS__)
#define LUX2_FE_19(f, x, ...) f(x) LUX2_FE_18(f, __VA_ARGS__)
#define LUX2_FE_20(f, x, ...) f(x) LUX2_FE_19(f, __VA_ARGS__)
#define LUX2_FE_N(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, \
                  _14, _15, _16, _17, _18, _19, _20, N, ...)              \
    N
#define LUX2_FOR_EACH(f, ...)                                              \
    LUX2_FE_N(__VA_ARGS__, LUX2_FE_20, LUX2_FE_19, LUX2_FE_18, LUX2_FE_17, \
              LUX2_FE_16, LUX2_FE_15, LUX2_FE_14, LUX2_FE_13, LUX2_FE_12,  \
              LUX2_FE_11, LUX2_FE_10, LUX2_FE_9, LUX2_FE_8, LUX2_FE_7,     \
              LUX2_FE_6, LUX2_FE_5, LUX2_FE_4, LUX2_FE_3, LUX2_FE_2,       \
              LUX2_FE_1)                                                   \
    (f, __VA_ARGS__)

    // True when a Blend overload already exists for T, i.e. T is a lane struct
    // whose fields are themselves blended.
    template <class T>
    struct HasBlend
    {
        template <class U>
        static auto Test(int) -> decltype(Blend(MaskP(), std::declval<const U &>(),
                                                std::declval<const U &>()),
                                          std::true_type{});
        template <class U>
        static auto Test(...) -> std::false_type;
        static constexpr bool value = decltype(Test<T>(0))::value;
    };
    template <class T>
    inline constexpr bool has_blend_v = HasBlend<T>::value;

    // A SFINAE pair rather than `if constexpr`.
    template <class T>
    ENOKI_INLINE auto BlendField(const MaskP &m, const T &a, const T &b)
        -> std::enable_if_t<has_blend_v<T>, T>
    {
        return Blend(m, a, b);
    }
    template <class T>
    ENOKI_INLINE auto BlendField(const MaskP &m, const T &a, const T &b)
        -> std::enable_if_t<!has_blend_v<T>, T>
    {
        return enoki::select(m, a, b);
    }

#define LUX2_BLEND_FIELD(f) r.f = ::lux2::BlendField(m, a.f, b.f);
    // Generate Blend(mask, a, b) for a lane struct from its field names.
#define LUX2_BLEND(S, ...)                                         \
    inline S Blend(const ::lux2::MaskP &m, const S &a, const S &b) \
    {                                                              \
        S r;                                                       \
        LUX2_FOR_EACH(LUX2_BLEND_FIELD, __VA_ARGS__)               \
        return r;                                                  \
    }

    LUX2_BLEND(SpectrumWavelengthsP, w, binRGB, offsetRGB, binXYZ, offsetXYZ)

} // namespace lux2

#endif // LUX2_INTEGRATORS_LANE_BLEND_H
