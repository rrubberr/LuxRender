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

// Minimal scalar color reference extracted from the legacy LuxRender scalar
// color pipeline. Used ONLY by tests/color_check.cpp as the golden cross-check
// for the SoA (Enoki) pipeline in core/color.{h,cpp}. Never linked into
// liblux2.so.

#ifndef LUX2_TESTS_COLOR_REF_H
#define LUX2_TESTS_COLOR_REF_H

#include <cmath>
#include <memory>

namespace colorref {

constexpr int WAVELENGTH_SAMPLES = 4;
constexpr float WAVELENGTH_START = 380.f;
constexpr float WAVELENGTH_END = 720.f;

// ---------------------------------------------------------------------------
// RGBColor
// ---------------------------------------------------------------------------

struct RGBColor
{
    float c[3];

    RGBColor() : c{0.f, 0.f, 0.f} {}
    RGBColor(float v) : c{v, v, v} {}
    RGBColor(float r, float g, float b) : c{r, g, b} {}
};

// ---------------------------------------------------------------------------
// SPD
// ---------------------------------------------------------------------------

// A regularly-sampled spectral power distribution with linear
// interpolation, matching the legacy scalar SPD sampling math.
class SPD
{
public:
    SPD() : nSamples(0), lambdaMin(0.f), lambdaMax(0.f), invDelta(0.f) {}
    virtual ~SPD() = default;

    SPD(const SPD &) = delete;
    SPD &operator=(const SPD &) = delete;

    // Sample at n wavelengths; out-of-range wavelengths yield 0.
    void Sample(int n, const float lambda[], float *p) const
    {
        for (int i = 0; i < n; ++i)
        {
            if (nSamples <= 1 || lambda[i] < lambdaMin ||
                lambda[i] > lambdaMax)
            {
                p[i] = 0.f;
                continue;
            }
            const float x = (lambda[i] - lambdaMin) * invDelta;
            const unsigned b0 = Floor2UInt(x);
            const unsigned b1 = Min(b0 + 1, nSamples - 1);
            const float dx = x - float(b0);
            p[i] = Lerp(dx, samples[b0], samples[b1]);
        }
    }

    // Precompute bins/offsets for fast repeat sampling.
    void Offsets(int n, const float lambda[], int *bins, float *offsets) const
    {
        for (int i = 0; i < n; ++i)
        {
            if (nSamples <= 1 || lambda[i] < lambdaMin ||
                lambda[i] > lambdaMax)
            {
                bins[i] = -1;
                continue;
            }
            const float x = (lambda[i] - lambdaMin) * invDelta;
            bins[i] = int(Floor2UInt(x));
            offsets[i] = x - float(bins[i]);
        }
    }

    // Fast sampling from precomputed bins/offsets.
    void Sample(int n, const int bins[], const float offsets[], float *p) const
    {
        for (int i = 0; i < n; ++i)
        {
            if (bins[i] < 0 ||
                bins[i] >= static_cast<int>(nSamples - 1))
            {
                p[i] = 0.f;
                continue;
            }
            p[i] = Lerp(offsets[i], samples[bins[i]], samples[bins[i] + 1]);
        }
    }

    void InitRegular(float lMin, float lMax, const float *s, unsigned n)
    {
        lambdaMin = lMin;
        lambdaMax = lMax;
        const float delta = (lambdaMax - lambdaMin) / (n - 1);
        invDelta = 1.f / delta;
        nSamples = n;
        samples.reset(new float[n]);
        for (unsigned i = 0; i < n; ++i)
            samples[i] = s[i];
    }

    void ZeroSamples()
    {
        for (unsigned i = 0; i < nSamples; ++i)
            samples[i] = 0.f;
    }

    void AddWeighted(float w, const float *tbl)
    {
        for (unsigned i = 0; i < nSamples; ++i)
            samples[i] += tbl[i] * w;
    }

protected:
    void Scale(float s)
    {
        for (unsigned i = 0; i < nSamples; ++i)
            samples[i] *= s;
    }
    void Clamp()
    {
        for (unsigned i = 0; i < nSamples; ++i)
            if (!(samples[i] > 0.f))
                samples[i] = 0.f;
    }

    unsigned nSamples;
    float lambdaMin, lambdaMax;
    float invDelta;
    std::unique_ptr<float[]> samples;

private:
    static unsigned Floor2UInt(float val)
    {
        return val > 0.f ? static_cast<unsigned>(std::floor(val)) : 0u;
    }
    static unsigned Min(unsigned a, unsigned b) { return a < b ? a : b; }
    static float Lerp(float t, float v1, float v2)
    {
        return v1 + t * (v2 - v1);
    }
};

// A regularly-sampled SPD built from a constant data table.
class RegularSPD : public SPD
{
public:
    RegularSPD(const float *s, float lMin, float lMax, unsigned n)
    {
        InitRegular(lMin, lMax, s, n);
    }
    RegularSPD(const float *s, float lMin, float lMax, unsigned n, float scale)
    {
        InitRegular(lMin, lMax, s, n);
        Scale(scale);
    }
};

// ---------------------------------------------------------------------------
// Smits RGB -> SPD (reflectant E / illuminant D65)
// ---------------------------------------------------------------------------

class RGBReflSPD : public SPD
{
public:
    explicit RGBReflSPD(const RGBColor &s);
};

class RGBIllumSPD : public SPD
{
public:
    explicit RGBIllumSPD(const RGBColor &s);
};

// ---------------------------------------------------------------------------
// SpectrumWavelengths
// ---------------------------------------------------------------------------

class SpectrumWavelengths
{
public:
    SpectrumWavelengths() : single_w(0), single(false) {}

    void Sample(float u1)
    {
        single = false;
        u1 *= WAVELENGTH_SAMPLES;
        single_w = unsigned(u1);
        u1 -= float(single_w);

        // Sample new stratified wavelengths and precompute RGB/XYZ data.
        const float offset =
            (WAVELENGTH_END - WAVELENGTH_START) / WAVELENGTH_SAMPLES;
        float waveln = WAVELENGTH_START + u1 * offset;
        for (int i = 0; i < WAVELENGTH_SAMPLES; ++i)
        {
            w[i] = waveln;
            waveln += offset;
        }
        spd_w.Offsets(WAVELENGTH_SAMPLES, w, binsRGB, offsetsRGB);
        spd_ciex.Offsets(WAVELENGTH_SAMPLES, w, binsXYZ, offsetsXYZ);
    }

    float SampleSingle() const
    {
        single = true;
        return w[single_w];
    }

    float w[WAVELENGTH_SAMPLES]; // Wavelengths in nm

    unsigned single_w; // Chosen single wavelength bin
    mutable bool single; // Split to single

    int binsRGB[WAVELENGTH_SAMPLES], binsXYZ[WAVELENGTH_SAMPLES];
    float offsetsRGB[WAVELENGTH_SAMPLES], offsetsXYZ[WAVELENGTH_SAMPLES];

    static const RegularSPD spd_w, spd_c, spd_m, spd_y,
        spd_r, spd_g, spd_b, spd_ciex, spd_ciey, spd_ciez;
};

// ---------------------------------------------------------------------------
// SWCSpectrum
// ---------------------------------------------------------------------------

class SWCSpectrum
{
public:
    float c[WAVELENGTH_SAMPLES];

    SWCSpectrum(float v = 0.f)
    {
        for (int i = 0; i < WAVELENGTH_SAMPLES; ++i)
            c[i] = v;
    }

    SWCSpectrum(const SpectrumWavelengths &sw, const SPD &s)
    {
        s.Sample(WAVELENGTH_SAMPLES, sw.w, c);
    }

    // CIE luminance at the sw wavelengths (single-split aware).
    float Y(const SpectrumWavelengths &sw) const;
};

// ---------------------------------------------------------------------------
// XYZColor
// ---------------------------------------------------------------------------

class XYZColor
{
public:
    float c[3];

    XYZColor(const SpectrumWavelengths &sw, const SWCSpectrum &s);
};

} // namespace colorref

#endif // LUX2_TESTS_COLOR_REF_H
