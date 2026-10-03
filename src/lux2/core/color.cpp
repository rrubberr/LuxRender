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

#include "core/color.h"

#include "core/data/rgbE_32.h"
#include "core/data/rgbD65_32.h"
#include "core/data/xyzbasis.h"

namespace lux2
{

    // CIE matching functions carry this scale. Monochromatic SWA.
    static constexpr Float CIE_SCALE =
        683.f * (WAVELENGTH_END - WAVELENGTH_START);

    // ---------------------------------------------------------------------------
    // SpectrumWavelengthsP
    // ---------------------------------------------------------------------------

    void SpectrumWavelengthsP::Sample(const FloatP &u1)
    {
        // Uniform stratified wavelength over [START, END).
        FromWavelength(FloatP(WAVELENGTH_START) +
                       FloatP(WAVELENGTH_END - WAVELENGTH_START) * u1);
    }

    void SpectrumWavelengthsP::FromWavelength(const FloatP &wl)
    {
        w = wl;

        // Smits RGB basis bins.
        const FloatP xRGB = (wl - FloatP(WAVELENGTH_START)) *
                            FloatP(INV_SMITS_DELTA);
        binRGB = floor(xRGB);
        offsetRGB = xRGB - FloatP(binRGB);

        // CIE bins.
        const FloatP xCIE = wl - FloatP(CIE_START);
        binXYZ = floor(xCIE);
        offsetXYZ = xCIE - FloatP(binXYZ);
    }

    // ---------------------------------------------------------------------------
    // Regular SPD sampling
    // ---------------------------------------------------------------------------

    FloatP SampleRegular1(const float *table, const Int32P &bin,
                          const FloatP &offset)
    {
        const Int32P b0 = min(bin, Int32P(SMITS_BINS - 1));
        const Int32P b1 = min(b0 + 1, Int32P(SMITS_BINS - 1));
        const FloatP lo = gather<FloatP>(table, b0);
        const FloatP hi = gather<FloatP>(table, b1);
        // Enoki lerp(a, b, t) = a + t*(b - a); the parameter is the last argument.
        return lerp(lo, hi, offset);
    }

    // ---------------------------------------------------------------------------
    // RGB -> SWCSpectrum
    // ---------------------------------------------------------------------------

    namespace
    {

        SWCSpectrumP Basis(const float *table, const SpectrumWavelengthsP &sw,
                           const FloatP &weight)
        {
            return SampleRegular1(table, sw.binRGB, sw.offsetRGB) * weight;
        }

        template <typename T>
        const T *Pick(bool illuminant, const T *refl, const T *illum)
        {
            return illuminant ? illum : refl;
        }

    } // namespace

    SWCSpectrumP RGBToSmitsSPD(const RGBColorP &rgb,
                               const SpectrumWavelengthsP &sw,
                               bool illuminant)
    {
        const FloatP r = rgb[0], g = rgb[1], b = rgb[2];

        // Ordering masks.
        const MaskP rMin = (r <= g) & (r <= b);
        const MaskP gMin = (g <= r) & (g <= b);
        const MaskP bMin = ~(rMin | gMin);

        const MaskP gLeB = g <= b; // within rMin
        const MaskP rLeB = r <= b; // within gMin
        const MaskP rLeG = r <= g; // within bMin

        // Lane weights for the three Smits primaries.
        const FloatP wWhite = select(rMin, r, select(gMin, g, b));

        // Secondary/secondary-primary weights.
        const FloatP cyanW = select(gLeB, g - r, b - r);
        const FloatP magW = select(rLeB, r - g, b - g);
        const FloatP yelW = select(rLeG, r - b, g - b);

        const FloatP wBlue = select(rMin & gLeB, b - g,
                                    select(rMin & ~gLeB, FloatP(0.f),
                                           select(gMin & rLeB, b - r, FloatP(0.f))));
        const FloatP wGreen = select(rMin & ~gLeB, g - b,
                                     select(bMin & rLeG, g - r, FloatP(0.f)));
        const FloatP wRed = select(gMin & ~rLeB, r - b,
                                   select(bMin & ~rLeG, r - g, FloatP(0.f)));

        const float *white = Pick(illuminant, refrgb2spect_white, illumrgb2spect_white);
        const float *cyan = Pick(illuminant, refrgb2spect_cyan, illumrgb2spect_cyan);
        const float *magenta = Pick(illuminant, refrgb2spect_magenta, illumrgb2spect_magenta);
        const float *yellow = Pick(illuminant, refrgb2spect_yellow, illumrgb2spect_yellow);
        const float *red = Pick(illuminant, refrgb2spect_red, illumrgb2spect_red);
        const float *green = Pick(illuminant, refrgb2spect_green, illumrgb2spect_green);
        const float *blue = Pick(illuminant, refrgb2spect_blue, illumrgb2spect_blue);

        SWCSpectrumP result = Basis(white, sw, wWhite);
        result += Basis(cyan, sw, select(rMin, cyanW, FloatP(0.f)));
        result += Basis(magenta, sw, select(gMin, magW, FloatP(0.f)));
        result += Basis(yellow, sw, select(bMin, yelW, FloatP(0.f)));
        result += Basis(blue, sw, wBlue);
        result += Basis(green, sw, wGreen);
        result += Basis(red, sw, wRed);

        // Reflectant and illuminant tables differ only by their scale.
        const float scale = illuminant ? float(illumrgb2spect_scale)
                                       : float(refrgb2spect_scale);
        result *= FloatP(scale);
        if (illuminant)
            result = Clamped(result);

        return result;
    }

    // ---------------------------------------------------------------------------
    // CIE luminance / tristimulus
    // ---------------------------------------------------------------------------

    namespace
    {
        // Linearized CIE table sample at sw's wavelength, scaled by CIE_SCALE.
        FloatP SampleCIE(const float *table, const SpectrumWavelengthsP &sw)
        {
            const Int32P b = min(sw.binXYZ, Int32P(CIE_BINS - 2));
            const FloatP lo = gather<FloatP>(table, b);
            const FloatP hi = gather<FloatP>(table, b + 1);
            return lerp(lo, hi, sw.offsetXYZ) * CIE_SCALE;
        }
    } // namespace

    FloatP SWCY(const SWCSpectrumP &s, const SpectrumWavelengthsP &sw)
    {
        // Monochromatic luminance ciey(w) * s scaled by CIE_SCALE.
        return SampleCIE(CIE_Y, sw) * s;
    }

    XYZColorP SWCToXYZ(const SWCSpectrumP &s, const SpectrumWavelengthsP &sw)
    {
        // Monochromatic: cie(w) * s * 683 * (END - START) per channel. The
        // scalar reference divides spd_cie* by WAVELENGTH_SAMPLES and then
        // multiplies the single-sample result by WAVELENGTH_SAMPLES, so the
        // factors cancel and CIE_SCALE is the exact match.
        return XYZColorP(SampleCIE(CIE_X, sw) * s,
                         SampleCIE(CIE_Y, sw) * s,
                         SampleCIE(CIE_Z, sw) * s);
    }

} // namespace lux2
