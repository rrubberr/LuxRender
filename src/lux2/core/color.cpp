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

#include <luxrays/core/color/spds/data/rgbE_32.h>
#include <luxrays/core/color/spds/data/rgbD65_32.h>
#include <luxrays/core/color/spds/data/xyzbasis.h>

namespace lux2
{

    using namespace enoki;

    // Bring in the scalar data tables from luxrays.
    using luxrays::CIE_Y;
    using luxrays::illumrgb2spect_blue;
    using luxrays::illumrgb2spect_cyan;
    using luxrays::illumrgb2spect_green;
    using luxrays::illumrgb2spect_magenta;
    using luxrays::illumrgb2spect_red;
    using luxrays::illumrgb2spect_scale;
    using luxrays::illumrgb2spect_white;
    using luxrays::illumrgb2spect_yellow;
    using luxrays::refrgb2spect_blue;
    using luxrays::refrgb2spect_cyan;
    using luxrays::refrgb2spect_green;
    using luxrays::refrgb2spect_magenta;
    using luxrays::refrgb2spect_red;
    using luxrays::refrgb2spect_scale;
    using luxrays::refrgb2spect_white;
    using luxrays::refrgb2spect_yellow;

    // CIE matching functions carry this scale.
    static constexpr Float CIE_SCALE =
        683.f * (WAVELENGTH_END - WAVELENGTH_START) * INV_WAVELENGTH_SAMPLES;

    // ---------------------------------------------------------------------------
    // SpectrumWavelengthsP
    // ---------------------------------------------------------------------------

    void SpectrumWavelengthsP::Sample(const FloatP &u1)
    {
        FloatP s = u1 * FloatP(float(WAVELENGTH_SAMPLES));
        single_w = floor(s);
        s -= FloatP(single_w);

        const FloatP offset = FloatP(WAVELENGTH_END - WAVELENGTH_START) *
                              FloatP(INV_WAVELENGTH_SAMPLES);
        FloatP waveln = FloatP(WAVELENGTH_START) + s * offset;
        for (size_t i = 0; i < WAVELENGTH_SAMPLES; ++i)
        {
            w[i] = waveln;
            waveln += offset;

            // Smits RGB basis bins.
            const FloatP xRGB = (w[i] - FloatP(WAVELENGTH_START)) *
                                FloatP(INV_SMITS_DELTA);
            binsRGB[i] = floor(xRGB);
            offsetsRGB[i] = xRGB - FloatP(binsRGB[i]);

            // CIE bins.
            const FloatP xCIE = w[i] - FloatP(CIE_START);
            binsXYZ[i] = floor(xCIE);
            offsetsXYZ[i] = xCIE - FloatP(binsXYZ[i]);
        }

        single = MaskP(false);
    }

    void SpectrumWavelengthsP::FromWavelength(const FloatP &wl)
    {
        // Smits RGB basis bins.
        const FloatP xRGB = (wl - FloatP(WAVELENGTH_START)) *
                            FloatP(INV_SMITS_DELTA);
        const Int32P binRGB = floor(xRGB);
        const FloatP offRGB = xRGB - FloatP(binRGB);

        // CIE bins.
        const FloatP xCIE = wl - FloatP(CIE_START);
        const Int32P binXYZ = floor(xCIE);
        const FloatP offXYZ = xCIE - FloatP(binXYZ);

        for (size_t i = 0; i < WAVELENGTH_SAMPLES; ++i)
        {
            w[i] = wl;
            binsRGB[i] = binRGB;
            offsetsRGB[i] = offRGB;
            binsXYZ[i] = binXYZ;
            offsetsXYZ[i] = offXYZ;
        }
        single = MaskP(false);
    }

    FloatP SpectrumWavelengthsP::SampleSingle()
    {
        single = MaskP(true);
        // Selection among the wavelength slots.
        FloatP result = w[0];
        for (size_t i = 1; i < WAVELENGTH_SAMPLES; ++i)
            result = select(single_w == Int32P(int(i)), w[i], result);
        return result;
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

    SWCSpectrumP SampleRegular(const float *table,
                               const Int32P bins[WAVELENGTH_SAMPLES],
                               const FloatP offsets[WAVELENGTH_SAMPLES])
    {
        SWCSpectrumP result;
        for (size_t i = 0; i < WAVELENGTH_SAMPLES; ++i)
            result[i] = SampleRegular1(table, bins[i], offsets[i]);
        return result;
    }

    // ---------------------------------------------------------------------------
    // Smits RGB -> SWCSpectrum
    // ---------------------------------------------------------------------------

    namespace
    {

        SWCSpectrumP Basis(const float *table, const SpectrumWavelengthsP &sw,
                           const FloatP &weight)
        {
            return SampleRegular(table, sw.binsRGB, sw.offsetsRGB) * weight;
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
            result = result.Clamped();

        return result;
    }

    // ---------------------------------------------------------------------------
    // CIE luminance
    // ---------------------------------------------------------------------------

    FloatP SWCY(const SWCSpectrumP &s, const SpectrumWavelengthsP &sw)
    {
        // Multi-wavelength luminance: sum_j ciey(w_j) * s_j.
        FloatP multi = FloatP(0.f);
        for (size_t j = 0; j < WAVELENGTH_SAMPLES; ++j)
        {
            const Int32P b = min(sw.binsXYZ[j], Int32P(CIE_BINS - 2));
            const FloatP lo = gather<FloatP>(CIE_Y, b);
            const FloatP hi = gather<FloatP>(CIE_Y, b + 1);
            multi += lerp(lo, hi, sw.offsetsXYZ[j]) * s[j];
        }

        // Single wavelength luminance: ciey(w_single) * s_single * N.
        Int32P b = sw.binsXYZ[0];
        FloatP off = sw.offsetsXYZ[0];
        FloatP sSingle = s[0];
        for (size_t i = 1; i < WAVELENGTH_SAMPLES; ++i)
        {
            const MaskP sel = sw.single_w == Int32P(int(i));
            b = select(sel, sw.binsXYZ[i], b);
            off = select(sel, sw.offsetsXYZ[i], off);
            sSingle = select(sel, s[i], sSingle);
        }
        b = min(b, Int32P(CIE_BINS - 2));
        const FloatP singleY = lerp(gather<FloatP>(CIE_Y, b),
                                    gather<FloatP>(CIE_Y, b + 1), off) *
                               sSingle * FloatP(float(WAVELENGTH_SAMPLES));

        return select(sw.single, singleY, multi) * CIE_SCALE;
    }

} // namespace lux2
