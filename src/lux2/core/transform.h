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

#ifndef LUX2_TRANSFORM_H
#define LUX2_TRANSFORM_H

// Homogeneous coordinate transform, templated on the point type.

#include "core/geometry.h"

#include <enoki/matrix.h>
#include <enoki/transform.h>

namespace lux2 {

template <typename Point_> struct Transform_ {

    // Type declarations
    static constexpr size_t Size = Point_::Size;

    using Float  = enoki::value_t<Point_>;
    using Matrix = enoki::Matrix<Float, Size>;
    using Mask   = enoki::mask_t<Float>;
    using Scalar = enoki::scalar_t<Float>;

   // Fields
    Matrix matrix            = enoki::identity<Matrix>();
    Matrix inverse_transpose = enoki::identity<Matrix>();

    // Identity transform.
    Transform_() = default;

    // Initialize from a matrix (computes the inverse transpose).
    Transform_(const Matrix &value)
        : matrix(value),
          inverse_transpose(enoki::inverse_transpose(value)) { }

    // Initialize from a matrix and its inverse transpose.
    Transform_(const Matrix &matrix, const Matrix &inverse_transpose)
        : matrix(matrix), inverse_transpose(inverse_transpose) { }

    // Build from a 16-float array (like .lxs Transform/ConcatTransform).
    template <size_t N = Size, enoki::enable_if_t<N == 4> = 0>
    static Transform_ from_column_major(const Float *m) {
        return Transform_(Matrix::from_cols(
            Vector<Float, 4>(m[0],  m[1],  m[2],  m[3]),
            Vector<Float, 4>(m[4],  m[5],  m[6],  m[7]),
            Vector<Float, 4>(m[8],  m[9],  m[10], m[11]),
            Vector<Float, 4>(m[12], m[13], m[14], m[15])));
    }

    // Concatenate transformations.
    Transform_ operator*(const Transform_ &other) const {
        return Transform_(matrix * other.matrix,
                          inverse_transpose * other.inverse_transpose);
    }

    // Compute the inverse (just shuffles, no arithmetic).
    Transform_ inverse() const {
        return Transform_(enoki::transpose(inverse_transpose),
                          enoki::transpose(matrix));
    }

    // Get the translation part of the matrix.
    Vector<Float, Size - 1> translation() const {
        return enoki::head<Size - 1>(matrix.coeff(Size - 1));
    }

    bool operator==(const Transform_ &t) const {
        return matrix == t.matrix && inverse_transpose == t.inverse_transpose;
    }
    bool operator!=(const Transform_ &t) const {
        return matrix != t.matrix || inverse_transpose != t.inverse_transpose;
    }

    // Apply to point / vector / normal

    // Transform a point (divides by the homogeneous w).
    template <typename T, typename Expr = enoki::expr_t<Float, T>>
    Point<Expr, Size - 1> operator*(const Point<T, Size - 1> &arg) const {
        enoki::Array<Expr, Size> result = matrix.coeff(Size - 1);
        for (size_t i = 0; i < Size - 1; ++i)
            result = enoki::fmadd(matrix.coeff(i), arg.coeff(i), result);
        return enoki::head<Size - 1>(result) / result.coeff(Size - 1);
    }

    // Transform a vector (ignores translation).
    template <typename T, typename Expr = enoki::expr_t<Float, T>>
    Vector<Expr, Size - 1> operator*(const Vector<T, Size - 1> &arg) const {
        enoki::Array<Expr, Size> result = matrix.coeff(0);
        result *= arg.x();
        for (size_t i = 1; i < Size - 1; ++i)
            result = enoki::fmadd(matrix.coeff(i), arg.coeff(i), result);
        return enoki::head<Size - 1>(result);
    }

    // Transform a normal (uses the inverse transpose).
    template <typename T, typename Expr = enoki::expr_t<Float, T>>
    Normal<Expr, Size - 1> operator*(const Normal<T, Size - 1> &arg) const {
        enoki::Array<Expr, Size> result = inverse_transpose.coeff(0);
        result *= arg.x();
        for (size_t i = 1; i < Size - 1; ++i)
            result = enoki::fmadd(inverse_transpose.coeff(i), arg.coeff(i), result);
        return enoki::head<Size - 1>(result);
    }

    // Transform a point, skipping the homogeneous divide (affine only).
    template <typename T, typename Expr = enoki::expr_t<Float, T>>
    Point<Expr, Size - 1> transform_affine(const Point<T, Size - 1> &arg) const {
        enoki::Array<Expr, Size> result = matrix.coeff(Size - 1);
        for (size_t i = 0; i < Size - 1; ++i)
            result = enoki::fmadd(matrix.coeff(i), arg.coeff(i), result);
        return enoki::head<Size - 1>(result);
    }

    // Construction helpers

    // Translation transformation.
    static Transform_ translate(const Vector<Float, Size - 1> &v) {
        return Transform_(enoki::translate<Matrix>(v),
                          enoki::transpose(enoki::translate<Matrix>(-v)));
    }

    // Scale transformation.
    static Transform_ scale(const Vector<Float, Size - 1> &v) {
        // A diagonal matrix's transpose is itself.
        return Transform_(enoki::scale<Matrix>(v),
                          enoki::scale<Matrix>(enoki::rcp(v)));
    }

    // Rotation about an arbitrary 3D axis, in degrees.
    template <size_t N = Size, enoki::enable_if_t<N == 4> = 0>
    static Transform_ rotate(const Vector<Float, Size - 1> &axis,
                             const Float &angle) {
        Matrix m = enoki::rotate<Matrix>(axis, enoki::deg_to_rad(angle));
        return Transform_(m, m);
    }

    // 2D rotation, in degrees.
    template <size_t N = Size, enoki::enable_if_t<N == 3> = 0>
    static Transform_ rotate(const Float &angle) {
        Matrix m = enoki::rotate<Matrix>(enoki::deg_to_rad(angle));
        return Transform_(m, m);
    }

    // Perspective projection (maps [near, far] to [0, 1]).
    template <size_t N = Size, enoki::enable_if_t<N == 4> = 0>
    static Transform_ perspective(Float fov, Float near_, Float far_) {
        Float recip = 1.f / (far_ - near_);
        Float tan_  = enoki::tan(enoki::deg_to_rad(fov * .5f)),
              cot   = 1.f / tan_;

        Matrix trafo = enoki::diag<Matrix>(
            Vector<Float, Size>(cot, cot, far_ * recip, 0.f));
        trafo(2, 3) = -near_ * far_ * recip;
        trafo(3, 2) = 1.f;

        Matrix inv_trafo = enoki::diag<Matrix>(
            Vector<Float, Size>(tan_, tan_, 0.f, 1.f / near_));
        inv_trafo(2, 3) = 1.f;
        inv_trafo(3, 2) = (near_ - far_) / (far_ * near_);

        return Transform_(trafo, enoki::transpose(inv_trafo));
    }

    // Orthographic projection, maps Z to [0, 1].
    template <size_t N = Size, enoki::enable_if_t<N == 4> = 0>
    static Transform_ orthographic(Float near_, Float far_) {
        return scale({1.f, 1.f, 1.f / (far_ - near_)}) *
               translate({0.f, 0.f, -near_});
    }

    // Look-at camera transformation (camera-to-world).
    template <size_t N = Size, enoki::enable_if_t<N == 4> = 0>
    static Transform_ look_at(const Point<Float, 3> &origin,
                              const Point<Float, 3> &target,
                              const Vector<Float, 3> &up) {
        using Vector3 = Vector<Float, 3>;
        using Vector4 = Vector<Float, 4>;

        Vector3 dir   = enoki::normalize(target - origin);
        Vector3 left  = enoki::normalize(enoki::cross(up, dir));
        Vector3 new_up = enoki::cross(dir, left);

        Matrix result = Matrix::from_cols(
            enoki::concat(left,   Scalar(0)),
            enoki::concat(new_up, Scalar(0)),
            enoki::concat(dir,    Scalar(0)),
            enoki::concat(origin, Scalar(1)));

        // World-to-camera: rows are the (orthonormal) basis; the bottom row
        // carries -(basis . origin).
        Vector4 bottom(-enoki::dot(left, origin),
                       -enoki::dot(new_up, origin),
                       -enoki::dot(dir, origin),
                       Scalar(1));
        Matrix inverse = Matrix::from_rows(
            enoki::concat(left,   Scalar(0)),
            enoki::concat(new_up, Scalar(0)),
            enoki::concat(dir,    Scalar(0)),
            bottom);

        return Transform_(result, enoki::transpose(inverse));
    }

    // Properties

    // Test for a scale component.
    Mask has_scale() const {
        Mask mask(false);
        for (size_t i = 0; i < Size - 1; ++i) {
            for (size_t j = i; j < Size - 1; ++j) {
                Float sum = 0.f;
                for (size_t k = 0; k < Size - 1; ++k)
                    sum += matrix.coeff(i).coeff(k) * matrix.coeff(j).coeff(k);
                mask |= enoki::abs(sum - (i == j ? 1.f : 0.f)) > 1e-3f;
            }
        }
        return mask;
    }
};

// The 3D scalar transform used throughout lux2
using Transform = Transform_<Point4f>;

} // namespace lux2

#endif // LUX2_TRANSFORM_H
