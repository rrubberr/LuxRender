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

#ifndef LUX2_SPECTRUM_H
#define LUX2_SPECTRUM_H

// SWCSpectrumP analogue of luxrays::SWCSpectrum.

#include "core/vecp.h"

#include <enoki/array.h>

namespace lux2 {

// Pull Enoki's math/compare functions.
using namespace enoki;

// Number of wavelength samples per spectrum. Must match
// luxrays::WAVELENGTH_SAMPLES.
constexpr int WAVELENGTH_SAMPLES = 4;

// =======================================================================
// SWCSpectrumP (one FloatP per sampled wavelength)
// =======================================================================

struct SWCSpectrumP {
    // The 4 spectral samples, each a packet.
    FloatP c[WAVELENGTH_SAMPLES];

    // -----------------------------------------------------------------
    // Construction
    // -----------------------------------------------------------------

    SWCSpectrumP() = default;

    /// Broadcast a scalar to every wavelength sample and every lane.
    explicit SWCSpectrumP(Float v) {
        for (int i = 0; i < WAVELENGTH_SAMPLES; ++i)
            c[i] = FloatP(v);
    }

    /// Broadcast a packet to every wavelength sample.
    explicit SWCSpectrumP(const FloatP &v) {
        for (int i = 0; i < WAVELENGTH_SAMPLES; ++i)
            c[i] = v;
    }

    /// Build from 4 packets (one per wavelength sample).
    SWCSpectrumP(const FloatP &c0, const FloatP &c1,
                 const FloatP &c2, const FloatP &c3) {
        c[0] = c0; c[1] = c1; c[2] = c2; c[3] = c3;
    }

    // -----------------------------------------------------------------
    // Component-wise arithmetic (luxrays::SWCSpectrum operators)
    // -----------------------------------------------------------------

    SWCSpectrumP operator+(const SWCSpectrumP &s) const {
        SWCSpectrumP r;
        for (int i = 0; i < WAVELENGTH_SAMPLES; ++i)
            r.c[i] = c[i] + s.c[i];
        return r;
    }
    SWCSpectrumP &operator+=(const SWCSpectrumP &s) {
        for (int i = 0; i < WAVELENGTH_SAMPLES; ++i)
            c[i] += s.c[i];
        return *this;
    }

    SWCSpectrumP operator-(const SWCSpectrumP &s) const {
        SWCSpectrumP r;
        for (int i = 0; i < WAVELENGTH_SAMPLES; ++i)
            r.c[i] = c[i] - s.c[i];
        return r;
    }
    SWCSpectrumP &operator-=(const SWCSpectrumP &s) {
        for (int i = 0; i < WAVELENGTH_SAMPLES; ++i)
            c[i] -= s.c[i];
        return *this;
    }

    SWCSpectrumP operator*(const SWCSpectrumP &s) const {
        SWCSpectrumP r;
        for (int i = 0; i < WAVELENGTH_SAMPLES; ++i)
            r.c[i] = c[i] * s.c[i];
        return r;
    }
    SWCSpectrumP &operator*=(const SWCSpectrumP &s) {
        for (int i = 0; i < WAVELENGTH_SAMPLES; ++i)
            c[i] *= s.c[i];
        return *this;
    }

    SWCSpectrumP operator/(const SWCSpectrumP &s) const {
        SWCSpectrumP r;
        for (int i = 0; i < WAVELENGTH_SAMPLES; ++i)
            r.c[i] = c[i] / s.c[i];
        return r;
    }
    SWCSpectrumP &operator/=(const SWCSpectrumP &s) {
        for (int i = 0; i < WAVELENGTH_SAMPLES; ++i)
            c[i] /= s.c[i];
        return *this;
    }

    // Scalar operands, that mix a SWCSpectrum with a plain float.
    SWCSpectrumP operator*(Float a) const {
        SWCSpectrumP r;
        for (int i = 0; i < WAVELENGTH_SAMPLES; ++i)
            r.c[i] = c[i] * a;
        return r;
    }
    SWCSpectrumP operator/(Float a) const {
        SWCSpectrumP r;
        for (int i = 0; i < WAVELENGTH_SAMPLES; ++i)
            r.c[i] = c[i] / a;
        return r;
    }

    friend SWCSpectrumP operator*(Float a, const SWCSpectrumP &s) { return s * a; }

    // -----------------------------------------------------------------
    // Selection / reduction helpers used by the integrator
    // -----------------------------------------------------------------

    /// Per-lane component-wise clamp to [0, inf).
    SWCSpectrumP Clamped() const {
        SWCSpectrumP r;
        for (int i = 0; i < WAVELENGTH_SAMPLES; ++i)
            r.c[i] = enoki::max(c[i], FloatP(0.f));
        return r;
    }

    /// Per-lane maximum across wavelength samples (SWCSpectrum::Clamp/yMax).
    FloatP MaxComponent() const {
        FloatP m = c[0];
        for (int i = 1; i < WAVELENGTH_SAMPLES; ++i)
            m = enoki::max(m, c[i]);
        return m;
    }

    /// Per-lane average across wavelength samples (for adaptive sampling / RR, like lux's y()).
    FloatP Average() const {
        FloatP s = c[0];
        for (int i = 1; i < WAVELENGTH_SAMPLES; ++i)
            s += c[i];
        return s * (1.f / WAVELENGTH_SAMPLES);
    }

    /// True if every sample of every lane is exactly zero.
    MaskP IsBlack() const {
        MaskP m = (c[0] == FloatP(0.f));
        for (int i = 1; i < WAVELENGTH_SAMPLES; ++i)
            m = m & (c[i] == FloatP(0.f));
        return m;
    }

    // Enoki nested-array/gather support to be added later if needed.
};

/// Component-wise select (mask ? a : b) for SWCSpectrumP.
inline SWCSpectrumP select(const MaskP &m, const SWCSpectrumP &a,
                           const SWCSpectrumP &b) {
    SWCSpectrumP r;
    for (int i = 0; i < WAVELENGTH_SAMPLES; ++i)
        r.c[i] = enoki::select(m, a.c[i], b.c[i]);
    return r;
}

/// Fused multiply-add across wavelength samples: a * b + c.
inline SWCSpectrumP fmadd(const SWCSpectrumP &a, const SWCSpectrumP &b,
                          const SWCSpectrumP &c) {
    SWCSpectrumP r;
    for (int i = 0; i < WAVELENGTH_SAMPLES; ++i)
        r.c[i] = enoki::fmadd(a.c[i], b.c[i], c.c[i]);
    return r;
}

} // namespace lux2

#endif // LUX2_SPECTRUM_H
