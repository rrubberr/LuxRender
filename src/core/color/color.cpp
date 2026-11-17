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

#include "core/color/color.h"

#include "core/color/data/rgbE_32.h"
#include "core/color/data/rgbD65_32.h"
#include "core/color/data/xyzbasis.h"

#include <cmath>
#include <vector>

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
        binRGB = floor2int<Int32P>(xRGB);
        offsetRGB = xRGB - FloatP(binRGB);

        // CIE bins.
        const FloatP xCIE = wl - FloatP(CIE_START);
        binXYZ = floor2int<Int32P>(xCIE);
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

    namespace
    {
        // The seven Smits weights plus the basis scale.
        struct SmitsW
        {
            FloatP wW, wC, wM, wY, wB, wG, wR;
            float scale;
            bool clamp;
        };

        SmitsW SmitsWeights(const RGBColorP &rgb, bool illuminant)
        {
            const FloatP r = rgb[0], g = rgb[1], b = rgb[2];

            // Ordering masks.
            const MaskP rMin = (r <= g) & (r <= b);
            const MaskP gMin = (g <= r) & (g <= b);
            const MaskP bMin = ~(rMin | gMin);

            const MaskP gLeB = g <= b; // within rMin
            const MaskP rLeB = r <= b; // within gMin
            const MaskP rLeG = r <= g; // within bMin

            SmitsW W;
            W.wW = select(rMin, r, select(gMin, g, b));
            W.wC = select(rMin, select(gLeB, g - r, b - r), FloatP(0.f));
            W.wM = select(gMin, select(rLeB, r - g, b - g), FloatP(0.f));
            W.wY = select(bMin, select(rLeG, r - b, g - b), FloatP(0.f));
            W.wB = select(rMin & gLeB, b - g,
                          select(rMin & ~gLeB, FloatP(0.f),
                                 select(gMin & rLeB, b - r, FloatP(0.f))));
            W.wG = select(rMin & ~gLeB, g - b,
                          select(bMin & rLeG, g - r, FloatP(0.f)));
            W.wR = select(gMin & ~rLeB, r - b,
                          select(bMin & ~rLeG, r - g, FloatP(0.f)));
            // Reflectant and illuminant tables differ by their scale.
            W.scale = illuminant ? float(illumrgb2spect_scale)
                                 : float(refrgb2spect_scale);
            W.clamp = illuminant;
            return W;
        }
    } // namespace

    SWCSpectrumP RGBToSmitsSPD(const RGBColorP &rgb,
                               const SpectrumWavelengthsP &sw,
                               bool illuminant)
    {
        const SmitsW W = SmitsWeights(rgb, illuminant);

        const float *white = Pick(illuminant, refrgb2spect_white, illumrgb2spect_white);
        const float *cyan = Pick(illuminant, refrgb2spect_cyan, illumrgb2spect_cyan);
        const float *magenta = Pick(illuminant, refrgb2spect_magenta, illumrgb2spect_magenta);
        const float *yellow = Pick(illuminant, refrgb2spect_yellow, illumrgb2spect_yellow);
        const float *red = Pick(illuminant, refrgb2spect_red, illumrgb2spect_red);
        const float *green = Pick(illuminant, refrgb2spect_green, illumrgb2spect_green);
        const float *blue = Pick(illuminant, refrgb2spect_blue, illumrgb2spect_blue);

        SWCSpectrumP result = Basis(white, sw, W.wW);
        result += Basis(cyan, sw, W.wC);
        result += Basis(magenta, sw, W.wM);
        result += Basis(yellow, sw, W.wY);
        result += Basis(blue, sw, W.wB);
        result += Basis(green, sw, W.wG);
        result += Basis(red, sw, W.wR);

        result *= FloatP(W.scale);
        if (W.clamp)
            result = Clamped(result);

        return result;
    }

    void BuildSmitsTable(const RGBColorP &rgb, bool illuminant,
                         float out[SMITS_BINS])
    {
        // Constant weights and scale go into a table so evaluation, so
        // sum_k w_k * lerp(t_k) == lerp(sum_k w_k t_k).
        const SmitsW W = SmitsWeights(rgb, illuminant);

        const float *white = Pick(illuminant, refrgb2spect_white, illumrgb2spect_white);
        const float *cyan = Pick(illuminant, refrgb2spect_cyan, illumrgb2spect_cyan);
        const float *magenta = Pick(illuminant, refrgb2spect_magenta, illumrgb2spect_magenta);
        const float *yellow = Pick(illuminant, refrgb2spect_yellow, illumrgb2spect_yellow);
        const float *red = Pick(illuminant, refrgb2spect_red, illumrgb2spect_red);
        const float *green = Pick(illuminant, refrgb2spect_green, illumrgb2spect_green);
        const float *blue = Pick(illuminant, refrgb2spect_blue, illumrgb2spect_blue);

        // rgb is a broadcast color, the weights are uniform across lanes.
        const float wW = W.wW[0], wC = W.wC[0], wM = W.wM[0], wY = W.wY[0];
        const float wB = W.wB[0], wG = W.wG[0], wR = W.wR[0];
        for (int i = 0; i < SMITS_BINS; ++i)
            out[i] = W.scale * (wW * white[i] + wC * cyan[i] +
                                wM * magenta[i] + wY * yellow[i] +
                                wB * blue[i] + wG * green[i] + wR * red[i]);
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
        // Monochromatic cie(w) * s * 683 * (END - START) per channel.
        return XYZColorP(SampleCIE(CIE_X, sw) * s,
                         SampleCIE(CIE_Y, sw) * s,
                         SampleCIE(CIE_Z, sw) * s);
    }

    float RGBIlluminanceY(const RGBColor &s)
    {
        // Reconstruct the illuminant Smits SPD samples,
        // then integrate against CIE_Y.
        const float r = s.r(), g = s.g(), b = s.b();
        const unsigned n = illumrgb2spect_bins;
        const float lmin = illumrgb2spect_start;
        const float lmax = illumrgb2spect_end;
        const float delta = (lmax - lmin) / float(n - 1);
        const float invDelta = 1.f / delta;

        std::vector<float> spd(n, 0.f);
        auto addW = [&](float w, const float *c) {
            for (unsigned i = 0; i < n; ++i)
                spd[i] += c[i] * w;
        };
        if (r <= g && r <= b)
        {
            addW(r, illumrgb2spect_white);
            if (g <= b) { addW(g - r, illumrgb2spect_cyan);   addW(b - g, illumrgb2spect_blue); }
            else        { addW(b - r, illumrgb2spect_cyan);   addW(g - b, illumrgb2spect_green); }
        }
        else if (g <= r && g <= b)
        {
            addW(g, illumrgb2spect_white);
            if (r <= b) { addW(r - g, illumrgb2spect_magenta); addW(b - r, illumrgb2spect_blue); }
            else        { addW(b - g, illumrgb2spect_magenta); addW(r - b, illumrgb2spect_red); }
        }
        else
        {
            addW(b, illumrgb2spect_white);
            if (r <= g) { addW(r - b, illumrgb2spect_yellow); addW(g - r, illumrgb2spect_green); }
            else        { addW(g - b, illumrgb2spect_yellow); addW(r - g, illumrgb2spect_red); }
        }

        // SPD::Scale(illumrgb2spect_scale) then Clamp().
        for (unsigned i = 0; i < n; ++i)
        {
            float v = spd[i] * illumrgb2spect_scale;
            spd[i] = (v > 0.f) ? v : 0.f;
        }

        // SPD::Sample(lambda): 0 outside [lmin, lmax], else linear interp.
        // Y() sums over the CIE grid and scales by 683. Accumulate in double.
        double y = 0.0;
        for (unsigned i = 0; i < nCIE; ++i)
        {
            const float lambda = float(CIEstart + i);
            if (lambda < lmin || lambda > lmax)
                continue;
            const float x = (lambda - lmin) * invDelta;
            const unsigned b0 = static_cast<unsigned>(x);
            const unsigned b1 = (b0 + 1 < n) ? b0 + 1 : n - 1;
            const float dx = x - float(b0);
            y += double(spd[b0] + dx * (spd[b1] - spd[b0])) * CIE_Y[i];
        }
        return float(y * 683.0);
    }

    // ---------------------------------------------------------------------------
    // Blackbody
    // ---------------------------------------------------------------------------

    namespace
    {
        // BlackbodySPD cache grid.
        constexpr float BB_CACHE_START = 380.f;
        constexpr float BB_CACHE_END = 720.f;
        constexpr int BB_CACHE_SAMPLES = 256;

        // Linear interpolation on the sample grid at wavelength lambda (nm).
        float SampleBB(const float *samples, float lambda)
        {
            constexpr float delta =
                (BB_CACHE_END - BB_CACHE_START) / float(BB_CACHE_SAMPLES - 1);
            constexpr float invDelta = 1.f / delta;
            if (lambda < BB_CACHE_START || lambda > BB_CACHE_END)
                return 0.f;
            const float x = (lambda - BB_CACHE_START) * invDelta;
            const int b0 = static_cast<int>(x);
            const int b1 = b0 + 1 < BB_CACHE_SAMPLES ? b0 + 1 : BB_CACHE_SAMPLES - 1;
            const float dx = x - static_cast<float>(b0);
            return samples[b0] + dx * (samples[b1] - samples[b0]);
        }
    } // namespace

    XYZColor BlackbodyToXYZ(float tempK)
    {
        // Fill the Planck curve, then normalize to peak and clamp.
        float samples[BB_CACHE_SAMPLES];
        const float delta =
            (BB_CACHE_END - BB_CACHE_START) / float(BB_CACHE_SAMPLES - 1);
        for (int i = 0; i < BB_CACHE_SAMPLES; ++i)
        {
            const float w = 1e-9f * (BB_CACHE_START + delta * i);
            samples[i] = 0.4e-9f * (3.74183e-16f * std::pow(w, -5.f)) /
                         (std::exp(1.4388e-2f / (w * tempK)) - 1.f);
        }

        float max = 0.f;
        for (int i = 0; i < BB_CACHE_SAMPLES; ++i)
            if (samples[i] > max)
                max = samples[i];
        // Very low temperature.
        if (!(max > 0.f))
            return XYZColor(0.f);
        const float scale = 1.f / max;
        for (int i = 0; i < BB_CACHE_SAMPLES; ++i)
        {
            samples[i] *= scale;
            if (!(samples[i] > 0.f))
                samples[i] = 0.f;
        }

        // Integrate against the CIE matching functions over the full table.
        XYZColor c(0.f);
        for (unsigned int i = 0; i < nCIE; ++i)
        {
            const float s = SampleBB(samples, static_cast<float>(CIEstart + i));
            c[0] += s * CIE_X[i];
            c[1] += s * CIE_Y[i];
            c[2] += s * CIE_Z[i];
        }
        return c * 683.f;
    }

} // namespace lux2
