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

// Number of components in an RGBColor.
constexpr int RGB_SAMPLES = 3;

// =======================================================================
// SWCSpectrum Templates
// =======================================================================

template <typename Value>
struct SWCSpectrum_ {
    using Scalar = enoki::scalar_t<Value>;
    using Mask   = enoki::mask_t<Value>;

    // The 4 spectral samples, each a Value.
    Value c[WAVELENGTH_SAMPLES];

    // -----------------------------------------------------------------
    // Construction
    // -----------------------------------------------------------------

    SWCSpectrum_() = default;

    // Broadcast a Value to every wavelength sample.
    explicit SWCSpectrum_(Value v) {
        for (int i = 0; i < WAVELENGTH_SAMPLES; ++i)
            c[i] = v;
    }

    // Build from 4 values (one per wavelength sample).
    SWCSpectrum_(const Value &c0, const Value &c1,
                 const Value &c2, const Value &c3) {
        c[0] = c0; c[1] = c1; c[2] = c2; c[3] = c3;
    }

    // -----------------------------------------------------------------
    // Component-wise arithmetic (luxrays::SWCSpectrum operators)
    // -----------------------------------------------------------------

    SWCSpectrum_ operator+(const SWCSpectrum_ &s) const {
        SWCSpectrum_ r;
        for (int i = 0; i < WAVELENGTH_SAMPLES; ++i)
            r.c[i] = c[i] + s.c[i];
        return r;
    }
    SWCSpectrum_ &operator+=(const SWCSpectrum_ &s) {
        for (int i = 0; i < WAVELENGTH_SAMPLES; ++i)
            c[i] += s.c[i];
        return *this;
    }

    SWCSpectrum_ operator-(const SWCSpectrum_ &s) const {
        SWCSpectrum_ r;
        for (int i = 0; i < WAVELENGTH_SAMPLES; ++i)
            r.c[i] = c[i] - s.c[i];
        return r;
    }
    SWCSpectrum_ &operator-=(const SWCSpectrum_ &s) {
        for (int i = 0; i < WAVELENGTH_SAMPLES; ++i)
            c[i] -= s.c[i];
        return *this;
    }

    SWCSpectrum_ operator*(const SWCSpectrum_ &s) const {
        SWCSpectrum_ r;
        for (int i = 0; i < WAVELENGTH_SAMPLES; ++i)
            r.c[i] = c[i] * s.c[i];
        return r;
    }
    SWCSpectrum_ &operator*=(const SWCSpectrum_ &s) {
        for (int i = 0; i < WAVELENGTH_SAMPLES; ++i)
            c[i] *= s.c[i];
        return *this;
    }

    SWCSpectrum_ operator/(const SWCSpectrum_ &s) const {
        SWCSpectrum_ r;
        for (int i = 0; i < WAVELENGTH_SAMPLES; ++i)
            r.c[i] = c[i] / s.c[i];
        return r;
    }
    SWCSpectrum_ &operator/=(const SWCSpectrum_ &s) {
        for (int i = 0; i < WAVELENGTH_SAMPLES; ++i)
            c[i] /= s.c[i];
        return *this;
    }

    // Scalar operands, that mix a SWCSpectrum with a plain float.
    SWCSpectrum_ operator*(Scalar a) const {
        SWCSpectrum_ r;
        for (int i = 0; i < WAVELENGTH_SAMPLES; ++i)
            r.c[i] = c[i] * a;
        return r;
    }
    SWCSpectrum_ operator/(Scalar a) const {
        SWCSpectrum_ r;
        for (int i = 0; i < WAVELENGTH_SAMPLES; ++i)
            r.c[i] = c[i] / a;
        return r;
    }

    // Avoid the redefinition that a template friend-in-class would trigger.
    friend SWCSpectrum_ operator*(Scalar a, const SWCSpectrum_ &s) {
        return s * a;
    }

    // -----------------------------------------------------------------
    // Selection / reduction helpers used by the integrator
    // -----------------------------------------------------------------

    // Per-lane component-wise clamp to [0, inf).
    SWCSpectrum_ Clamped() const {
        SWCSpectrum_ r;
        for (int i = 0; i < WAVELENGTH_SAMPLES; ++i)
            r.c[i] = enoki::max(c[i], Value(0.f));
        return r;
    }

    // Per-lane maximum across wavelength samples (SWCSpectrum::Clamp/yMax).
    Value MaxComponent() const {
        Value m = c[0];
        for (int i = 1; i < WAVELENGTH_SAMPLES; ++i)
            m = enoki::max(m, c[i]);
        return m;
    }

    // Per-lane average across wavelength samples (for adaptive sampling / RR, like lux's y()).
    Value Average() const {
        Value s = c[0];
        for (int i = 1; i < WAVELENGTH_SAMPLES; ++i)
            s += c[i];
        return s * (Scalar(1.f) / Scalar(WAVELENGTH_SAMPLES));
    }

    // True if every sample of every lane is exactly zero.
    Mask IsBlack() const {
        Mask m = (c[0] == Value(0.f));
        for (int i = 1; i < WAVELENGTH_SAMPLES; ++i)
            m = m & (c[i] == Value(0.f));
        return m;
    }

    // Enoki nested-array/gather support to be added later if needed.
};

// Scalar and packet aliases.
using SWCSpectrum  = SWCSpectrum_<Float>;
using SWCSpectrumP = SWCSpectrum_<FloatP>;

// Component-wise select (mask ? a : b) for SWCSpectrum_.
template <typename Value>
SWCSpectrum_<Value> select(const enoki::mask_t<Value> &m,
                           const SWCSpectrum_<Value> &a,
                           const SWCSpectrum_<Value> &b) {
    SWCSpectrum_<Value> r;
    for (int i = 0; i < WAVELENGTH_SAMPLES; ++i)
        r.c[i] = enoki::select(m, a.c[i], b.c[i]);
    return r;
}

// Fused multiply-add across wavelength samples: a * b + c.
template <typename Value>
SWCSpectrum_<Value> fmadd(const SWCSpectrum_<Value> &a,
                          const SWCSpectrum_<Value> &b,
                          const SWCSpectrum_<Value> &c) {
    SWCSpectrum_<Value> r;
    for (int i = 0; i < WAVELENGTH_SAMPLES; ++i)
        r.c[i] = enoki::fmadd(a.c[i], b.c[i], c.c[i]);
    return r;
}

// =======================================================================
// RGBColor
// =======================================================================

struct RGBColor {
    Float r = 0.f, g = 0.f, b = 0.f;

    RGBColor() = default;
    RGBColor(Float v) : r(v), g(v), b(v) { }
    RGBColor(Float r, Float g, Float b) : r(r), g(g), b(b) { }
    // Read three contiguous floats.
    explicit RGBColor(const Float *v) : r(v[0]), g(v[1]), b(v[2]) { }

    RGBColor operator+(const RGBColor &c) const { return {r + c.r, g + c.g, b + c.b}; }
    RGBColor operator-(const RGBColor &c) const { return {r - c.r, g - c.g, b - c.b}; }
    RGBColor operator*(const RGBColor &c) const { return {r * c.r, g * c.g, b * c.b}; }
    RGBColor operator*(Float a) const { return {r * a, g * a, b * a}; }
    RGBColor operator/(Float a) const { return {r / a, g / a, b / a}; }

    RGBColor &operator+=(const RGBColor &c) { r += c.r; g += c.g; b += c.b; return *this; }
    RGBColor &operator*=(const RGBColor &c) { r *= c.r; g *= c.g; b *= c.b; return *this; }

    friend RGBColor operator*(Float a, const RGBColor &c) { return c * a; }
};

} // namespace lux2

#endif // LUX2_SPECTRUM_H
