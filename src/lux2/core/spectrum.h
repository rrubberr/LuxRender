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

#include "core/vecp.h"

#include <enoki/array.h>

namespace lux2
{

    // Pull Enoki's math/compare functions.
    using namespace enoki;

    // Number of wavelength samples per spectrum.
    constexpr int WAVELENGTH_SAMPLES = 4;

    // Number of components in an RGBColor.
    constexpr int RGB_SAMPLES = 3;

    // Number of components in an XYZColor.
    constexpr int XYZ_SAMPLES = 3;

    // ---------------------------------------------------------------------------
    // SWCSpectrum Templates
    // ---------------------------------------------------------------------------

    // A spectrum of WAVELENGTH_SAMPLES spectral samples.
    template <typename Value_, size_t Size_ = WAVELENGTH_SAMPLES>
    struct SWCSpectrum_
        : enoki::StaticArrayImpl<Value_, Size_, false, SWCSpectrum_<Value_, Size_>>
    {
        using Base =
            enoki::StaticArrayImpl<Value_, Size_, false, SWCSpectrum_<Value_, Size_>>;

        // Helper alias used to implement Enoki type promotion.
        template <typename T>
        using ReplaceValue = SWCSpectrum_<T, Size_>;

        using ArrayType = SWCSpectrum_;
        using MaskType = enoki::Mask<Value_, Size_>;

        ENOKI_ARRAY_IMPORT(Base, SWCSpectrum_)

        // -----------------------------------------------------------------
        // Reductions across the wavelength axis
        // -----------------------------------------------------------------

        // Clamp to [0, inf).
        SWCSpectrum_ Clamped() const { return max(*this, Value_(0.f)); }

        // Maximum across wavelength samples.
        Value_ MaxComponent() const { return hmax(*this); }

        // Average across wavelength samples.
        Value_ Average() const { return hmean(*this); }

        // True where every wavelength sample is exactly zero.
        auto IsBlack() const { return all(eq(*this, Value_(0.f))); }
    };

    // Lane masking.
    template <typename Value_, size_t Size_>
    struct SWCSpectrum_<enoki::detail::MaskedArray<Value_>, Size_>
        : enoki::detail::MaskedArray<SWCSpectrum_<Value_, Size_>>
    {
        using Base = enoki::detail::MaskedArray<SWCSpectrum_<Value_, Size_>>;
        using Base::Base;
        using Base::operator=;
        SWCSpectrum_(const Base &b) : Base(b) {}
    };

    // Scalar and packet aliases.

    // With monochromatic SWA,  each lane carries a single wavelength.
    using SWCSpectrum = SWCSpectrum_<Float>;
    using SWCSpectrumP = FloatP;

    inline FloatP Clamped(const FloatP &s) { return enoki::max(s, FloatP(0.f)); }
    inline MaskP IsBlack(const FloatP &s) { return eq(s, FloatP(0.f)); }

    // ---------------------------------------------------------------------------
    // RGBColor
    // ---------------------------------------------------------------------------

    // An RGB color of RGB_SAMPLES components templated on the value type.
    template <typename Value_, size_t Size_ = RGB_SAMPLES>
    struct RGBColor_
        : enoki::StaticArrayImpl<Value_, Size_, false, RGBColor_<Value_, Size_>>
    {
        using Base =
            enoki::StaticArrayImpl<Value_, Size_, false, RGBColor_<Value_, Size_>>;

        // Helper alias used to implement Enoki type promotion.
        template <typename T>
        using ReplaceValue = RGBColor_<T, Size_>;

        using ArrayType = RGBColor_;
        using MaskType = enoki::Mask<Value_, Size_>;

        ENOKI_ARRAY_IMPORT(Base, RGBColor_)

        // Chromatic component accessors.
        Value_ r() const { return (*this)[0]; }
        Value_ g() const { return (*this)[1]; }
        Value_ b() const { return (*this)[2]; }

        // Rec.709 luma.
        Value_ Y() const
        {
            return Value_(0.212671f) * (*this)[0] +
                   Value_(0.715160f) * (*this)[1] +
                   Value_(0.072169f) * (*this)[2];
        }

        // Unweighted mean of the channels.
        Value_ Filter() const { return hmean(*this); }

        // Componentwise power.
        RGBColor_ Pow(const Value_ &e) const
        {
            return pow(max(*this, Value_(0.f)), e);
        }
    };

    // Lane masking.
    template <typename Value_, size_t Size_>
    struct RGBColor_<enoki::detail::MaskedArray<Value_>, Size_>
        : enoki::detail::MaskedArray<RGBColor_<Value_, Size_>>
    {
        using Base = enoki::detail::MaskedArray<RGBColor_<Value_, Size_>>;
        using Base::Base;
        using Base::operator=;
        RGBColor_(const Base &b) : Base(b) {}
    };

    // Scalar and packet aliases.
    using RGBColor = RGBColor_<Float>;
    using RGBColorP = RGBColor_<FloatP>;

    // ---------------------------------------------------------------------------
    // XYZColor
    // ---------------------------------------------------------------------------

    // A CIE XYZ color of XYZ_SAMPLES components.
    template <typename Value_, size_t Size_ = XYZ_SAMPLES>
    struct XYZColor_
        : enoki::StaticArrayImpl<Value_, Size_, false, XYZColor_<Value_, Size_>>
    {
        using Base =
            enoki::StaticArrayImpl<Value_, Size_, false, XYZColor_<Value_, Size_>>;

        // Helper alias used to implement Enoki type promotion.
        template <typename T>
        using ReplaceValue = XYZColor_<T, Size_>;

        using ArrayType = XYZColor_;
        using MaskType = enoki::Mask<Value_, Size_>;

        ENOKI_ARRAY_IMPORT(Base, XYZColor_)

        // Tristimulus accessors.
        Value_ X() const { return (*this)[0]; }
        Value_ Y() const { return (*this)[1]; }
        Value_ Z() const { return (*this)[2]; }
    };

    // Lane masking.
    template <typename Value_, size_t Size_>
    struct XYZColor_<enoki::detail::MaskedArray<Value_>, Size_>
        : enoki::detail::MaskedArray<XYZColor_<Value_, Size_>>
    {
        using Base = enoki::detail::MaskedArray<XYZColor_<Value_, Size_>>;
        using Base::Base;
        using Base::operator=;
        XYZColor_(const Base &b) : Base(b) {}
    };

    // Scalar and packet aliases.
    using XYZColor = XYZColor_<Float>;
    using XYZColorP = XYZColor_<FloatP>;

} // namespace lux2

#endif // LUX2_SPECTRUM_H
