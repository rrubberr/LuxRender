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

#include "core/colorsystem.h"

#include <enoki/matrix.h>

#include <limits>

namespace lux2
{

    namespace
    {

        using Mat3 = enoki::Matrix<float, 3>;
        using Vec3 = enoki::Array<float, 3>;

        // Invert a matrix, rejecting singular or ill-conditioned input.
        bool Invert3x3(const Mat3 &in, Mat3 &out)
        {
            if (enoki::det(in) == 0.f)
                return false;
            out = enoki::inverse(in);

            // Condition number check: floats have 7 digits of precision,
            // demand at least 4. frob() is the squared Frobenius norm.
            const float eps = std::numeric_limits<float>::epsilon();
            if (enoki::frob(in) * enoki::frob(out) > (0.25e-8f / (eps * eps)))
                return false;

            return true;
        }

        // Is any primary negative (below the gamut)?
        inline bool LowGamut(const RGBColor &color)
        {
            return color[0] < 0.f || color[1] < 0.f || color[2] < 0.f;
        }

        // Is any primary above 1 (beyond the gamut)?
        inline bool HighGamut(const RGBColor &color)
        {
            return color[0] > 1.f || color[1] > 1.f || color[2] > 1.f;
        }

    } // namespace

    ColorSystem::ColorSystem(float xR, float yR, float xG, float yG,
                             float xB, float yB, float xW, float yW, float lum)
        : xRed(xR), yRed(yR), xGreen(xG), yGreen(yG), xBlue(xB), yBlue(yB),
          xWhite(xW), yWhite(yW), luminance(lum)
    {
        const Vec3 red(xRed / yRed, 1.f, (1.f - xRed - yRed) / yRed);
        const Vec3 green(xGreen / yGreen, 1.f,
                         (1.f - xGreen - yGreen) / yGreen);
        const Vec3 blue(xBlue / yBlue, 1.f, (1.f - xBlue - yBlue) / yBlue);
        const Vec3 white(xWhite / yWhite, 1.f,
                         (1.f - xWhite - yWhite) / yWhite);

        // Columns are the primaries' XYZ coordinates.
        Mat3 rgb = Mat3::from_cols(red, green, blue);
        Invert3x3(rgb, rgb);

        // Emissive weights of the primaries at the white point.
        const Vec3 y = rgb * white;
        const Vec3 x = y * Vec3(red.x(), green.x(), blue.x());
        const Vec3 z = y * Vec3(red.z(), green.z(), blue.z());

        // Column j is v_j plus the scalar white_j broadcast (R*Tt).
        rgb = Mat3::from_cols(x + white.x(), y + white.y(), z + white.z());

        // Gram matrix G(i, j) = (v_i . v_j + w_i * w_j) * luminance,
        // where v = {x, y, z} and w = white.
        const Mat3 rows = Mat3::from_rows(x, y, z);
        const Mat3 outer = Mat3::from_cols(white * white.x(),
                                           white * white.y(),
                                           white * white.z());
        Mat3 g = (rows * enoki::transpose(rows) + outer) * luminance;
        Invert3x3(g, g);

        // C=R*Tt*(T*Tt)^-1
        const Mat3 xyz_to_rgb = rgb * g;
        Mat3 rgb_to_xyz;
        Invert3x3(xyz_to_rgb, rgb_to_xyz);

        // Members are indexed [row][col]; Enoki's (i, j) is row i, col j.
        for (size_t i = 0; i < 3; ++i)
        {
            for (size_t j = 0; j < 3; ++j)
            {
                XYZToRGB[i][j] = xyz_to_rgb(i, j);
                RGBToXYZ[i][j] = rgb_to_xyz(i, j);
            }
        }
    }

    RGBColor ColorSystem::ToRGB(const XYZColor &color) const
    {
        return RGBColor(
            XYZToRGB[0][0] * color[0] + XYZToRGB[0][1] * color[1] +
                XYZToRGB[0][2] * color[2],
            XYZToRGB[1][0] * color[0] + XYZToRGB[1][1] * color[1] +
                XYZToRGB[1][2] * color[2],
            XYZToRGB[2][0] * color[0] + XYZToRGB[2][1] * color[1] +
                XYZToRGB[2][2] * color[2]);
    }

    RGBColor ColorSystem::ToRGBConstrained(const XYZColor &color) const
    {
        RGBColor rgb(ToRGB(color));
        Constrain(color, rgb);
        return rgb;
    }

    XYZColor ColorSystem::ToXYZ(const RGBColor &color) const
    {
        return XYZColor(
            RGBToXYZ[0][0] * color[0] + RGBToXYZ[0][1] * color[1] +
                RGBToXYZ[0][2] * color[2],
            RGBToXYZ[1][0] * color[0] + RGBToXYZ[1][1] * color[1] +
                RGBToXYZ[1][2] * color[2],
            RGBToXYZ[2][0] * color[0] + RGBToXYZ[2][1] * color[1] +
                RGBToXYZ[2][2] * color[2]);
    }

    // Desaturate a negative weight RGB shade by projecting it onto the
    // edge of the Maxwell triangle formed by the three primaries.
    bool ColorSystem::Constrain(const XYZColor &xyz, RGBColor &rgb) const
    {
        bool constrain = false;
        if (LowGamut(rgb))
        {
            const float YComp = xyz.Y();
            if (!(YComp > 0.f))
            {
                rgb = RGBColor(0.f);
                return true;
            }
            float xComp = xyz[0] / (xyz[0] + xyz[1] + xyz[2]);
            float yComp = xyz[1] / (xyz[0] + xyz[1] + xyz[2]);

            // Blue to Red line equation.
            const float aBR = (yRed - yBlue) / (xRed - xBlue);
            const float bBR = yBlue - (aBR * xBlue);

            // Blue to Green line equation.
            const float aBG = (yGreen - yBlue) / (xGreen - xBlue);
            const float bBG = yBlue - (aBG * xBlue);

            // Green to Red line equation.
            const float aGR = (yRed - yGreen) / (xRed - xGreen);
            const float bGR = yGreen - (aGR * xGreen);

            if (yComp < (aBR * xComp + bBR))
            {
                // Below the BR line: orthogonal projection onto BR.
                const float aOrtho = -1.f / aBR;
                const float bOrtho = -aOrtho * xComp + yComp;

                xComp = (bOrtho - bBR) / (aBR - aOrtho);
                yComp = aBR * xComp + bBR;

                if (xComp < xBlue)
                {
                    xComp = xBlue;
                    yComp = yBlue;
                }
                else if (xComp > xRed)
                {
                    xComp = xRed;
                    yComp = yRed;
                }
            }
            else if (yComp > (aBG * xComp + bBG))
            {
                const float aOrtho = -1.f / aBG;
                const float bOrtho = -aOrtho * xComp + yComp;

                xComp = (bOrtho - bBG) / (aBG - aOrtho);
                yComp = aBG * xComp + bBG;

                if (xComp < xBlue)
                {
                    xComp = xBlue;
                    yComp = yBlue;
                }
                else if (xComp > xGreen)
                {
                    xComp = xGreen;
                    yComp = yGreen;
                }
            }
            else if (yComp > (aGR * xComp + bGR))
            {
                const float aOrtho = -1.f / aGR;
                const float bOrtho = -aOrtho * xComp + yComp;

                xComp = (bOrtho - bGR) / (aGR - aOrtho);
                yComp = aGR * xComp + bGR;

                if (xComp < xGreen)
                {
                    xComp = xGreen;
                    yComp = yGreen;
                }
                else if (xComp > xRed)
                {
                    xComp = xRed;
                    yComp = yRed;
                }
            }

            // Recompute XYZ from the constrained xyY and convert.
            XYZColor disp((xComp * YComp) / yComp, YComp,
                          (1.f - xComp - yComp) * YComp / yComp);
            rgb = ToRGB(disp);
            constrain = true;
        }

        return constrain;
    }

    RGBColor ColorSystem::Limit(const RGBColor &rgb, int method) const
    {
        if (HighGamut(rgb))
        {
            switch (method)
            {
            case 2:
                return clamp(rgb, RGBColor(0.f), RGBColor(1.f));
            case 3:
                return lerp(RGBColor(0.f), rgb,
                            1.f / max(rgb[0], max(rgb[1], rgb[2])));
            default:
            {
                const float lum = (method == 0)
                                      ? (RGBToXYZ[1][0] * rgb[0] +
                                         RGBToXYZ[1][1] * rgb[1] +
                                         RGBToXYZ[1][2] * rgb[2])
                                      : (luminance / 3.f);
                if (lum > luminance)
                    return RGBColor(1.f);

                // Parameter of the point on the vector from the white point
                // to the requested color, on the primary with greater weight.
                const float l = lum / luminance;
                float parameter;
                if (rgb[0] > rgb[1] && rgb[0] > rgb[2])
                {
                    parameter = (1.f - l) / (rgb[0] - l);
                }
                else if (rgb[1] > rgb[2])
                {
                    parameter = (1.f - l) / (rgb[1] - l);
                }
                else
                {
                    parameter = (1.f - l) / (rgb[2] - l);
                }

                return lerp(RGBColor(l), rgb, parameter);
            }
            }
        }
        return rgb;
    }

    const ColorSystem ColorSystem::DefaultColorSystem;

    // ---------------------------------------------------------------------------
    // Bradford ColorAdaptator
    // ---------------------------------------------------------------------------

    namespace
    {
        constexpr float bradford[3][3] = {
            {0.8951f, 0.2664f, -0.1614f},
            {-0.7502f, 1.7135f, 0.0367f},
            {0.0389f, -0.0685f, 1.0296f}};
        constexpr float invBradford[3][3] = {
            {0.9869929f, -0.1470543f, 0.1599627f},
            {0.4323053f, 0.5183603f, 0.0492912f},
            {-0.0085287f, 0.0400428f, 0.9684867f}};

        // result = a * b (Multiply3x3).
        void Multiply3x3(const float a[3][3], const float b[3][3],
                         float result[3][3])
        {
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    result[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] +
                                   a[i][2] * b[2][j];
        }

        // result = m * v (row i dot v) (Transform3x3).
        void Transform3x3(const float m[3][3], const float v[3], float r[3])
        {
            for (int i = 0; i < 3; ++i)
                r[i] = m[i][0] * v[0] + m[i][1] * v[1] + m[i][2] * v[2];
        }
    } // namespace

    ColorAdaptator::ColorAdaptator(const XYZColor &from, const XYZColor &to)
    {
        const float mat[3][3] = {
            {to[0] / from[0], 0.f, 0.f},
            {0.f, to[1] / from[1], 0.f},
            {0.f, 0.f, to[2] / from[2]}};
        float temp[3][3];
        Multiply3x3(mat, bradford, temp);
        Multiply3x3(invBradford, temp, conv);
    }

    XYZColor ColorAdaptator::Adapt(const XYZColor &color) const
    {
        float out[3];
        Transform3x3(conv, color.data(), out);
        return XYZColor(out[0], out[1], out[2]);
    }

    ColorAdaptator ColorAdaptator::operator*(const ColorAdaptator &ca) const
    {
        ColorAdaptator result(XYZColor(1.f), XYZColor(1.f));
        Multiply3x3(conv, ca.conv, result.conv);
        return result;
    }

    ColorAdaptator &ColorAdaptator::operator*=(float s)
    {
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                conv[i][j] *= s;
        return *this;
    }

} // namespace lux2
