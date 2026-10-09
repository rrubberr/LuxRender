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

#ifndef LUX2_COLORSYSTEM_H
#define LUX2_COLORSYSTEM_H

#include "core/spectrum.h"

namespace lux2
{

    // A color system is defined by the CIE x and y coordinates of its
    // three primary illuminants and the x and y coordinates of the white
    // point. The additional definition of the white point intensity allows
    // for intensity adaptation. Default is SMPTE.
    class ColorSystem
    {
    public:
        ColorSystem(float xR = .63f, float yR = .34f,
                    float xG = .31f, float yG = .595f,
                    float xB = .155f, float yB = .07f,
                    float xW = .314275f, float yW = .329411f,
                    float lum = 1.f);

        // XYZ -> RGB via the stored conversion matrix.
        RGBColor ToRGB(const XYZColor &color) const;

        // XYZ -> RGB, desaturating outside-gamut colors via Constrain().
        RGBColor ToRGBConstrained(const XYZColor &color) const;

        // RGB -> XYZ via the stored conversion matrix.
        XYZColor ToXYZ(const RGBColor &color) const;

        // Desaturate an outside-gamut RGB (given its XYZ) to the closest
        // representation within the primaries' Maxwell triangle. Returns
        // true if rgb was modified.
        bool Constrain(const XYZColor &xyz, RGBColor &rgb) const;

        // Clamp an out-of-range [0,1] RGB by one of four methods:
        // 0 = lum, 1 = hue, 2 = cut, 3 = darken.
        RGBColor Limit(const RGBColor &rgb, int method) const;

        static const ColorSystem DefaultColorSystem;

        float xRed, yRed;
        float xGreen, yGreen;
        float xBlue, yBlue;
        float xWhite, yWhite;
        float luminance;
        float XYZToRGB[3][3];
        float RGBToXYZ[3][3];
    };

    // Color space white point conversion for blackbody.
    class ColorAdaptator
    {
    public:
        // Build the conversion from the initial color to the final color.
        ColorAdaptator(const XYZColor &from, const XYZColor &to);

        // Convert a color into the adapted colorspace.
        XYZColor Adapt(const XYZColor &color) const;

        // Composition of two adaptators (this applied after ca).
        ColorAdaptator operator*(const ColorAdaptator &ca) const;

        // Scale every matrix entry by s.
        ColorAdaptator &operator*=(float s);

    private:
        float conv[3][3];
    };

} // namespace lux2

#endif // LUX2_COLORSYSTEM_H
