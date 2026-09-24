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

#ifndef LUX2_COLOR_H
#define LUX2_COLOR_H

#include "core/vecp.h"
#include "core/spectrum.h"

#include <enoki/array.h>

namespace lux2
{

    // ---------------------------------------------------------------------------
    // Wavelength sampling range and SPD table
    // ---------------------------------------------------------------------------

    constexpr Float WAVELENGTH_START = 380.f;
    constexpr Float WAVELENGTH_END = 720.f;
    constexpr Float INV_WAVELENGTH_SAMPLES = 1.f / Float(WAVELENGTH_SAMPLES);

    // Both the reflectant (E) and illuminant (D65) tables share this range.
    constexpr int SMITS_BINS = 32;
    constexpr Float SMITS_DELTA = (WAVELENGTH_END - WAVELENGTH_START) /
                                  Float(SMITS_BINS - 1);
    constexpr Float INV_SMITS_DELTA = 1.f / SMITS_DELTA;

    // CIE matching function table.
    constexpr int CIE_START = 360;
    constexpr int CIE_END = 830;
    constexpr int CIE_BINS = CIE_END - CIE_START + 1; // 471, delta == 1nm

    // ---------------------------------------------------------------------------
    // SpectrumWavelengthsP
    // ---------------------------------------------------------------------------

    // A set of wavelengths.
    struct SpectrumWavelengthsP
    {
        FloatP w[WAVELENGTH_SAMPLES]; // wavelengths in nm

        Int32P single_w; // chosen single-wavelength bin per lane
        MaskP single;    // true where the lane is split to single

        Int32P binsRGB[WAVELENGTH_SAMPLES];
        FloatP offsetsRGB[WAVELENGTH_SAMPLES];
        Int32P binsXYZ[WAVELENGTH_SAMPLES];
        FloatP offsetsXYZ[WAVELENGTH_SAMPLES];

        // Stratified wavelength sampling from a uniform [0,1) sample.
        void Sample(const FloatP &u1);

        // Populate every wavelength slot and precompute the CIE bins.
        void FromWavelength(const FloatP &wl);

        // Split to a single wavelength.
        FloatP SampleSingle();
    };

    // ---------------------------------------------------------------------------
    // Regular SPD sampling
    // ---------------------------------------------------------------------------

    // Linearly interpolate a table at the precomputed offset pairs.
    SWCSpectrumP SampleRegular(const float *table,
                               const Int32P bins[WAVELENGTH_SAMPLES],
                               const FloatP offsets[WAVELENGTH_SAMPLES]);

    // Gather a regularly-sampled table at a single precomputed bin/offset.
    FloatP SampleRegular1(const float *table, const Int32P &bin,
                          const FloatP &offset);

    // ---------------------------------------------------------------------------
    // Smits RGB -> SWCSpectrum reconstruction
    // ---------------------------------------------------------------------------

    // Reconstruct a spectrum from an RGB color at the wavelengths in sw.
    SWCSpectrumP RGBToSmitsSPD(const RGBColorP &rgb,
                               const SpectrumWavelengthsP &sw,
                               bool illuminant);

    // ---------------------------------------------------------------------------
    // CIE luminance / tristimulus
    // ---------------------------------------------------------------------------

    // Photometric luminance of a spectrum.
    FloatP SWCY(const SWCSpectrumP &s, const SpectrumWavelengthsP &sw);

} // namespace lux2

#endif // LUX2_COLOR_H
