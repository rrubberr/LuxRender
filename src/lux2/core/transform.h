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

#include "core/geometry.h"

#include <enoki/matrix.h>
#include <enoki/transform.h>

namespace lux2
{

    template <typename Point_>
    struct Transform_
    {

        // Type declarations.
        static constexpr size_t Size = Point_::Size;

        using Float = enoki::value_t<Point_>;
        using Matrix = enoki::Matrix<Float, Size>;
        using Mask = enoki::mask_t<Float>;
        using Scalar = enoki::scalar_t<Float>;

        // Fields.
        Matrix matrix = enoki::identity<Matrix>();
        Matrix inverse_transpose = enoki::identity<Matrix>();

        // Identity transform.
        Transform_() = default;

        // Initialize from a matrix.
        Transform_(const Matrix &value)
            : matrix(value),
              inverse_transpose(enoki::inverse_transpose(value)) {}

        // Initialize from a matrix and its inverse transpose.
        Transform_(const Matrix &matrix, const Matrix &inverse_transpose)
            : matrix(matrix), inverse_transpose(inverse_transpose) {}

        // Build from a 16-float array.
        template <size_t N = Size, enoki::enable_if_t<N == 4> = 0>
        static Transform_ from_column_major(const Float *m)
        {
            return Transform_(Matrix::from_cols(
                Vector<Float, 4>(m[0], m[1], m[2], m[3]),
                Vector<Float, 4>(m[4], m[5], m[6], m[7]),
                Vector<Float, 4>(m[8], m[9], m[10], m[11]),
                Vector<Float, 4>(m[12], m[13], m[14], m[15])));
        }

        // Concatenate transformations.
        Transform_ operator*(const Transform_ &other) const
        {
            return Transform_(matrix * other.matrix,
                              inverse_transpose * other.inverse_transpose);
        }

        // Compute the inverse.
        Transform_ inverse() const
        {
            return Transform_(enoki::transpose(inverse_transpose),
                              enoki::transpose(matrix));
        }

        // Get the translation part of the matrix.
        Vector<Float, Size - 1> translation() const
        {
            return enoki::head<Size - 1>(matrix.coeff(Size - 1));
        }

        bool operator==(const Transform_ &t) const
        {
            return matrix == t.matrix && inverse_transpose == t.inverse_transpose;
        }
        bool operator!=(const Transform_ &t) const
        {
            return matrix != t.matrix || inverse_transpose != t.inverse_transpose;
        }

        // Transform a point.
        template <typename T, typename Expr = enoki::expr_t<Float, T>>
        Point<Expr, Size - 1> operator*(const Point<T, Size - 1> &arg) const
        {
            enoki::Array<Expr, Size> result = matrix.coeff(Size - 1);
            for (size_t i = 0; i < Size - 1; ++i)
                result = enoki::fmadd(matrix.coeff(i), arg.coeff(i), result);
            return enoki::head<Size - 1>(result) / result.coeff(Size - 1);
        }

        // Transform a vector.
        template <typename T, typename Expr = enoki::expr_t<Float, T>>
        Vector<Expr, Size - 1> operator*(const Vector<T, Size - 1> &arg) const
        {
            enoki::Array<Expr, Size> result = matrix.coeff(0);
            result *= arg.x();
            for (size_t i = 1; i < Size - 1; ++i)
                result = enoki::fmadd(matrix.coeff(i), arg.coeff(i), result);
            return enoki::head<Size - 1>(result);
        }

        // Transform a normal.
        template <typename T, typename Expr = enoki::expr_t<Float, T>>
        Normal<Expr, Size - 1> operator*(const Normal<T, Size - 1> &arg) const
        {
            enoki::Array<Expr, Size> result = inverse_transpose.coeff(0);
            result *= arg.x();
            for (size_t i = 1; i < Size - 1; ++i)
                result = enoki::fmadd(inverse_transpose.coeff(i), arg.coeff(i), result);
            return enoki::head<Size - 1>(result);
        }

        // Transform a point, skipping the homogeneous divide.
        template <typename T, typename Expr = enoki::expr_t<Float, T>>
        Point<Expr, Size - 1> transform_affine(const Point<T, Size - 1> &arg) const
        {
            enoki::Array<Expr, Size> result = matrix.coeff(Size - 1);
            for (size_t i = 0; i < Size - 1; ++i)
                result = enoki::fmadd(matrix.coeff(i), arg.coeff(i), result);
            return enoki::head<Size - 1>(result);
        }

        // Translation transformation.
        static Transform_ translate(const Vector<Float, Size - 1> &v)
        {
            return Transform_(enoki::translate<Matrix>(v),
                              enoki::transpose(enoki::translate<Matrix>(-v)));
        }

        // Scale transformation.
        static Transform_ scale(const Vector<Float, Size - 1> &v)
        {
            // A diagonal matrix's transpose is itself.
            return Transform_(enoki::scale<Matrix>(v),
                              enoki::scale<Matrix>(enoki::rcp(v)));
        }

        // Rotation about an arbitrary 3D axis, in degrees.
        template <size_t N = Size, enoki::enable_if_t<N == 4> = 0>
        static Transform_ rotate(const Vector<Float, Size - 1> &axis,
                                 const Float &angle)
        {
            Matrix m = enoki::rotate<Matrix>(axis, enoki::deg_to_rad(angle));
            return Transform_(m, m);
        }

        // 2D rotation, in degrees.
        template <size_t N = Size, enoki::enable_if_t<N == 3> = 0>
        static Transform_ rotate(const Float &angle)
        {
            Matrix m = enoki::rotate<Matrix>(enoki::deg_to_rad(angle));
            return Transform_(m, m);
        }

        // Perspective projection (maps [near, far] to [0, 1]).
        template <size_t N = Size, enoki::enable_if_t<N == 4> = 0>
        static Transform_ perspective(Float fov, Float near_, Float far_)
        {
            Float recip = 1.f / (far_ - near_);
            Float tan_ = enoki::tan(enoki::deg_to_rad(fov * .5f)),
                  cot = 1.f / tan_;

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
        static Transform_ orthographic(Float near_, Float far_)
        {
            return scale({1.f, 1.f, 1.f / (far_ - near_)}) *
                   translate({0.f, 0.f, -near_});
        }

        // Look-at camera transformation (camera-to-world).
        template <size_t N = Size, enoki::enable_if_t<N == 4> = 0>
        static Transform_ look_at(const Point<Float, 3> &origin,
                                  const Point<Float, 3> &target,
                                  const Vector<Float, 3> &up)
        {
            using Vector3 = Vector<Float, 3>;
            using Vector4 = Vector<Float, 4>;

            Vector3 dir = enoki::normalize(target - origin);
            Vector3 right = enoki::normalize(enoki::cross(dir, up));
            Vector3 new_up = enoki::cross(right, dir);

            Matrix result = Matrix::from_cols(
                enoki::concat(right, Scalar(0)),
                enoki::concat(new_up, Scalar(0)),
                enoki::concat(dir, Scalar(0)),
                enoki::concat(origin, Scalar(1)));

            // Rows are the (orthonormal) basis.
            Vector4 bottom(-enoki::dot(right, origin),
                           -enoki::dot(new_up, origin),
                           -enoki::dot(dir, origin),
                           Scalar(1));
            Matrix inverse = Matrix::from_rows(
                enoki::concat(right, Scalar(0)),
                enoki::concat(new_up, Scalar(0)),
                enoki::concat(dir, Scalar(0)),
                bottom);

            return Transform_(result, enoki::transpose(inverse));
        }

        // Properties

        // True when the linear part has a negative determinant, i.e. the
        // transform mirrors space and flips surface orientation.
        template <size_t N = Size, enoki::enable_if_t<N == 4> = 0>
        bool SwapsHandedness() const
        {
            // m[row][col] = matrix.coeff(col).coeff(row); det of upper-left 3x3.
            const Scalar a00 = matrix.coeff(0).coeff(0),
                         a01 = matrix.coeff(1).coeff(0),
                         a02 = matrix.coeff(2).coeff(0),
                         a10 = matrix.coeff(0).coeff(1),
                         a11 = matrix.coeff(1).coeff(1),
                         a12 = matrix.coeff(2).coeff(1),
                         a20 = matrix.coeff(0).coeff(2),
                         a21 = matrix.coeff(1).coeff(2),
                         a22 = matrix.coeff(2).coeff(2);
            const Scalar det = a00 * (a11 * a22 - a12 * a21) -
                               a01 * (a10 * a22 - a12 * a20) +
                               a02 * (a10 * a21 - a11 * a20);
            return det < Scalar(0);
        }

        // Test for a scale component.
        Mask has_scale() const
        {
            Mask mask(false);
            for (size_t i = 0; i < Size - 1; ++i)
            {
                for (size_t j = i; j < Size - 1; ++j)
                {
                    Float sum = 0.f;
                    for (size_t k = 0; k < Size - 1; ++k)
                        sum += matrix.coeff(i).coeff(k) * matrix.coeff(j).coeff(k);
                    mask |= enoki::abs(sum - (i == j ? 1.f : 0.f)) > 1e-3f;
                }
            }
            return mask;
        }
    };

    // The 3D scalar transform used throughout.
    using Transform = Transform_<Point4f>;

} // namespace lux2

#endif // LUX2_TRANSFORM_H
