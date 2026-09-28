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

// Scalar reference implementation extracted from the legacy LuxRender color
// pipeline (SpectrumWavelengths / SWCSpectrum / XYZColor / RGB SPDs).
// See color_ref.h for scope and rationale.

#include "color_ref.h"

#include "../../core/data/rgbE_32.h"
#include "../../core/data/rgbD65_32.h"
#include "../../core/data/xyzbasis.h"

namespace colorref {

// ---------------------------------------------------------------------------
// SpectrumWavelengths static SPD tables
// ---------------------------------------------------------------------------

const RegularSPD SpectrumWavelengths::spd_w(lux2::refrgb2spect_white,
    lux2::refrgb2spect_start, lux2::refrgb2spect_end,
    lux2::refrgb2spect_bins, lux2::refrgb2spect_scale);

const RegularSPD SpectrumWavelengths::spd_c(lux2::refrgb2spect_cyan,
    lux2::refrgb2spect_start, lux2::refrgb2spect_end,
    lux2::refrgb2spect_bins, lux2::refrgb2spect_scale);

const RegularSPD SpectrumWavelengths::spd_m(lux2::refrgb2spect_magenta,
    lux2::refrgb2spect_start, lux2::refrgb2spect_end,
    lux2::refrgb2spect_bins, lux2::refrgb2spect_scale);

const RegularSPD SpectrumWavelengths::spd_y(lux2::refrgb2spect_yellow,
    lux2::refrgb2spect_start, lux2::refrgb2spect_end,
    lux2::refrgb2spect_bins, lux2::refrgb2spect_scale);

const RegularSPD SpectrumWavelengths::spd_r(lux2::refrgb2spect_red,
    lux2::refrgb2spect_start, lux2::refrgb2spect_end,
    lux2::refrgb2spect_bins, lux2::refrgb2spect_scale);

const RegularSPD SpectrumWavelengths::spd_g(lux2::refrgb2spect_green,
    lux2::refrgb2spect_start, lux2::refrgb2spect_end,
    lux2::refrgb2spect_bins, lux2::refrgb2spect_scale);

const RegularSPD SpectrumWavelengths::spd_b(lux2::refrgb2spect_blue,
    lux2::refrgb2spect_start, lux2::refrgb2spect_end,
    lux2::refrgb2spect_bins, lux2::refrgb2spect_scale);

const RegularSPD SpectrumWavelengths::spd_ciex(lux2::CIE_X, lux2::CIEstart,
    lux2::CIEend, lux2::nCIE,
    683.f * float(WAVELENGTH_END - WAVELENGTH_START) / WAVELENGTH_SAMPLES);

const RegularSPD SpectrumWavelengths::spd_ciey(lux2::CIE_Y, lux2::CIEstart,
    lux2::CIEend, lux2::nCIE,
    683.f * float(WAVELENGTH_END - WAVELENGTH_START) / WAVELENGTH_SAMPLES);

const RegularSPD SpectrumWavelengths::spd_ciez(lux2::CIE_Z, lux2::CIEstart,
    lux2::CIEend, lux2::nCIE,
    683.f * float(WAVELENGTH_END - WAVELENGTH_START) / WAVELENGTH_SAMPLES);

// ---------------------------------------------------------------------------
// SWCSpectrum
// ---------------------------------------------------------------------------

float SWCSpectrum::Y(const SpectrumWavelengths &sw) const
{
    float y = 0.f;

    if (sw.single)
    {
        const unsigned j = sw.single_w;
        SpectrumWavelengths::spd_ciey.Sample(1,
            sw.binsXYZ + j, sw.offsetsXYZ + j, &y);
        y *= c[j] * WAVELENGTH_SAMPLES;
    } else
    {
        float ciey[WAVELENGTH_SAMPLES];
        SpectrumWavelengths::spd_ciey.Sample(WAVELENGTH_SAMPLES,
            sw.binsXYZ, sw.offsetsXYZ, ciey);
        for (unsigned j = 0; j < WAVELENGTH_SAMPLES; ++j)
            y += ciey[j] * c[j];
    }

    return y;
}

// ---------------------------------------------------------------------------
// XYZColor
// ---------------------------------------------------------------------------

XYZColor::XYZColor(const SpectrumWavelengths &sw, const SWCSpectrum &s)
{
    if (sw.single)
    {
        const unsigned j = sw.single_w;
        SpectrumWavelengths::spd_ciex.Sample(1, sw.binsXYZ + j,
            sw.offsetsXYZ + j, c);
        SpectrumWavelengths::spd_ciey.Sample(1, sw.binsXYZ + j,
            sw.offsetsXYZ + j, c + 1);
        SpectrumWavelengths::spd_ciez.Sample(1, sw.binsXYZ + j,
            sw.offsetsXYZ + j, c + 2);
        c[0] *= s.c[j] * WAVELENGTH_SAMPLES;
        c[1] *= s.c[j] * WAVELENGTH_SAMPLES;
        c[2] *= s.c[j] * WAVELENGTH_SAMPLES;
    } else
    {
        float x[WAVELENGTH_SAMPLES], y[WAVELENGTH_SAMPLES],
            z[WAVELENGTH_SAMPLES];
        SpectrumWavelengths::spd_ciex.Sample(WAVELENGTH_SAMPLES,
            sw.binsXYZ, sw.offsetsXYZ, x);
        SpectrumWavelengths::spd_ciey.Sample(WAVELENGTH_SAMPLES,
            sw.binsXYZ, sw.offsetsXYZ, y);
        SpectrumWavelengths::spd_ciez.Sample(WAVELENGTH_SAMPLES,
            sw.binsXYZ, sw.offsetsXYZ, z);
        c[0] = c[1] = c[2] = 0.f;
        for (unsigned j = 0; j < WAVELENGTH_SAMPLES; ++j)
        {
            c[0] += x[j] * s.c[j];
            c[1] += y[j] * s.c[j];
            c[2] += z[j] * s.c[j];
        }
    }
}

// ---------------------------------------------------------------------------
// RGB SPDs (Smits)
// ---------------------------------------------------------------------------

namespace
{

// Common Smits basis blend: builds the 32-sample SPD from the seven basis
// tables weighted by the RGB color, matching the legacy RGBReflSPD /
// RGBIllumSPD init().
void SmitsBlend(SPD &spd, const RGBColor &s, float start, float end,
                unsigned bins, const float *white, const float *cyan,
                const float *magenta, const float *yellow, const float *red,
                const float *green, const float *blue)
{
    spd.InitRegular(start, end, white, bins);
    spd.ZeroSamples();

    const float r = s.c[0];
    const float g = s.c[1];
    const float b = s.c[2];

    if (r <= g && r <= b)
    {
        spd.AddWeighted(r, white);
        if (g <= b)
        {
            spd.AddWeighted(g - r, cyan);
            spd.AddWeighted(b - g, blue);
        } else
        {
            spd.AddWeighted(b - r, cyan);
            spd.AddWeighted(g - b, green);
        }
    } else if (g <= r && g <= b)
    {
        spd.AddWeighted(g, white);
        if (r <= b)
        {
            spd.AddWeighted(r - g, magenta);
            spd.AddWeighted(b - r, blue);
        } else
        {
            spd.AddWeighted(b - g, magenta);
            spd.AddWeighted(r - b, red);
        }
    } else // blue <= red && blue <= green
    {
        spd.AddWeighted(b, white);
        if (r <= g)
        {
            spd.AddWeighted(r - b, yellow);
            spd.AddWeighted(g - r, green);
        } else
        {
            spd.AddWeighted(g - b, yellow);
            spd.AddWeighted(r - g, red);
        }
    }
}

} // namespace

RGBReflSPD::RGBReflSPD(const RGBColor &s)
{
    SmitsBlend(*this, s, lux2::refrgb2spect_start, lux2::refrgb2spect_end,
        lux2::refrgb2spect_bins, lux2::refrgb2spect_white,
        lux2::refrgb2spect_cyan, lux2::refrgb2spect_magenta,
        lux2::refrgb2spect_yellow, lux2::refrgb2spect_red,
        lux2::refrgb2spect_green, lux2::refrgb2spect_blue);
    Scale(lux2::refrgb2spect_scale);
    // Don't clamp: negative values are needed for scattering.
}

RGBIllumSPD::RGBIllumSPD(const RGBColor &s)
{
    SmitsBlend(*this, s, lux2::illumrgb2spect_start, lux2::illumrgb2spect_end,
        lux2::illumrgb2spect_bins, lux2::illumrgb2spect_white,
        lux2::illumrgb2spect_cyan, lux2::illumrgb2spect_magenta,
        lux2::illumrgb2spect_yellow, lux2::illumrgb2spect_red,
        lux2::illumrgb2spect_green, lux2::illumrgb2spect_blue);
    Scale(lux2::illumrgb2spect_scale);
    Clamp();
}

} // namespace colorref
