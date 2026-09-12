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

#ifndef LUX2_GEOMETRY_H
#define LUX2_GEOMETRY_H

// Point/Vector/Normal as Enoki static arrays.

#include "core/vecp.h"

#include <utility>

namespace lux2 {

// =======================================================================
// Vector, point, and normal data types
// =======================================================================

template <typename Value_, size_t Size_>
struct Vector : enoki::StaticArrayImpl<Value_, Size_, false, Vector<Value_, Size_>> {
    using Base = enoki::StaticArrayImpl<Value_, Size_, false, Vector<Value_, Size_>>;

    template <typename T> using ReplaceValue = Vector<T, Size_>;

    using ArrayType = Vector;
    using MaskType = enoki::Mask<Value_, Size_>;

    ENOKI_ARRAY_IMPORT(Base, Vector)
};

template <typename Value_, size_t Size_>
struct Point : enoki::StaticArrayImpl<Value_, Size_, false, Point<Value_, Size_>> {
    using Base = enoki::StaticArrayImpl<Value_, Size_, false, Point<Value_, Size_>>;

    template <typename T> using ReplaceValue = Point<T, Size_>;

    using ArrayType = Point;
    using MaskType = enoki::Mask<Value_, Size_>;

    ENOKI_ARRAY_IMPORT(Base, Point)
};

template <typename Value_, size_t Size_>
struct Normal : enoki::StaticArrayImpl<Value_, Size_, false, Normal<Value_, Size_>> {
    using Base = enoki::StaticArrayImpl<Value_, Size_, false, Normal<Value_, Size_>>;

    template <typename T> using ReplaceValue = Normal<T, Size_>;

    using ArrayType = Normal;
    using MaskType = enoki::Mask<Value_, Size_>;

    ENOKI_ARRAY_IMPORT(Base, Normal)
};

/// Subtracting two points always yields a vector.
template <typename T1, size_t S1, typename T2, size_t S2>
auto operator-(const Point<T1, S1> &p1, const Point<T2, S2> &p2) {
    return Vector<T1, S1>(p1) - Vector<T2, S2>(p2);
}

/// Subtracting a vector from a point yields a point.
template <typename T1, size_t S1, typename T2, size_t S2>
auto operator-(const Point<T1, S1> &p1, const Vector<T2, S2> &v2) {
    return p1 - Point<T2, S2>(v2);
}

/// Adding a vector to a point yields a point.
template <typename T1, size_t S1, typename T2, size_t S2>
auto operator+(const Point<T1, S1> &p1, const Vector<T2, S2> &v2) {
    return p1 + Point<T2, S2>(v2);
}

// =======================================================================
// Masking for the geometry types
// =======================================================================

template <typename Value_, size_t Size_>
struct Vector<enoki::detail::MaskedArray<Value_>, Size_>
    : enoki::detail::MaskedArray<Vector<Value_, Size_>> {
    using Base = enoki::detail::MaskedArray<Vector<Value_, Size_>>;
    using Base::Base;
    using Base::operator=;
    Vector(const Base &b) : Base(b) { }
};

template <typename Value_, size_t Size_>
struct Point<enoki::detail::MaskedArray<Value_>, Size_>
    : enoki::detail::MaskedArray<Point<Value_, Size_>> {
    using Base = enoki::detail::MaskedArray<Point<Value_, Size_>>;
    using Base::Base;
    using Base::operator=;
    Point(const Base &b) : Base(b) { }
};

template <typename Value_, size_t Size_>
struct Normal<enoki::detail::MaskedArray<Value_>, Size_>
    : enoki::detail::MaskedArray<Normal<Value_, Size_>> {
    using Base = enoki::detail::MaskedArray<Normal<Value_, Size_>>;
    using Base::Base;
    using Base::operator=;
    Normal(const Base &b) : Base(b) { }
};

// =======================================================================
// 3D packet typedefs
// =======================================================================

using Point3fP = Point<FloatP, 3>;
using Vector3fP = Vector<FloatP, 3>;
using Normal3fP = Normal<FloatP, 3>;

/// Complete the unit vector 'n' into an orthonormal basis {b, c, n}.
/// Duff et al., "Building an Orthonormal Basis, Revisited" (JCGT 2017),
/// From mitsuba2 (core/vector.h).
template <typename Vector3fP_>
std::pair<Vector3fP_, Vector3fP_> coordinate_system(const Vector3fP_ &n) {
    static_assert(Vector3fP_::Size == 3,
        "coordinate_system() expects a 3D vector as input!");

    using FloatP_ = enoki::value_t<Vector3fP_>;

    FloatP_ sign = enoki::sign(n.z()),
            a = -enoki::rcp(sign + n.z()),
            b = n.x() * n.y() * a;

    return {
        Vector3fP_(enoki::fmadd(enoki::sqr(n.x()) * a, n.z(), FloatP_(1.f)),
                   enoki::mulsign(b, n.z()),
                   enoki::mulsign_neg(n.x(), n.z())),
        Vector3fP_(b, enoki::fmadd(enoki::sqr(n.y()), a, sign), -n.y())
    };
}

} // namespace lux2

#endif // LUX2_GEOMETRY_H
