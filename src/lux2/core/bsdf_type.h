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

#ifndef LUX2_BSDF_TYPE_H
#define LUX2_BSDF_TYPE_H

#include <cstdint>

#include "core/vecp.h"

namespace lux2 {

// BSDF lobe-type bitmask.
enum class BSDFType : uint32_t {
    None                = 0x00000,   // No lobes (empty).
    Null                = 0x00001,   // No deflection (pass-through).
    DiffuseReflection   = 0x00002,   // Lambertian reflection.
    DiffuseTransmission = 0x00004,   // Lambertian transmission.
    GlossyReflection    = 0x00008,   // Non-delta reflection lobe.
    GlossyTransmission  = 0x00010,   // Non-delta transmission lobe.
    SpecularReflection  = 0x00020,   // Delta reflection (lux BSDF_SPECULAR reflection).
    SpecularTransmission= 0x00040,   // Delta transmission (lux BSDF_SPECULAR transmission).
    Delta1DReflection   = 0x00080,   // Reserved: rough glass/metal 1D lobes (future).
    Delta1DTransmission = 0x00100,   // Reserved.
    Anisotropic         = 0x01000,   // Attribute: not rotation-invariant about normal.
    FrontSide           = 0x08000,   // Attribute: interacts on front-facing side.
    BackSide            = 0x10000,   // Attribute: interacts on back-facing side.

    // Compound constants.
    Reflection   = uint32_t(DiffuseReflection) | uint32_t(GlossyReflection) | uint32_t(SpecularReflection),
    Transmission = uint32_t(DiffuseTransmission) | uint32_t(GlossyTransmission) | uint32_t(SpecularTransmission) | uint32_t(Null),
    Diffuse      = uint32_t(DiffuseReflection) | uint32_t(DiffuseTransmission),
    Glossy       = uint32_t(GlossyReflection) | uint32_t(GlossyTransmission),
    Specular     = uint32_t(SpecularReflection) | uint32_t(SpecularTransmission),
    Smooth       = uint32_t(Diffuse) | uint32_t(Glossy),
    All          = uint32_t(Diffuse) | uint32_t(Glossy) | uint32_t(Specular) | uint32_t(Null),
};

// Bitwise OR of two BSDFType values.
constexpr uint32_t operator|(BSDFType a, BSDFType b) { return uint32_t(a) | uint32_t(b); }

// Bitwise AND of two BSDFType values.
constexpr uint32_t operator&(BSDFType a, BSDFType b) { return uint32_t(a) & uint32_t(b); }

// Bitwise OR of uint32_t and BSDFType, result cast back to BSDFType.
constexpr BSDFType operator|(uint32_t a, BSDFType b) { return BSDFType(a | uint32_t(b)); }

// Bitwise OR of BSDFType and uint32_t, result cast back to BSDFType.
constexpr BSDFType operator|(BSDFType a, uint32_t b) { return BSDFType(uint32_t(a) | b); }

// Bitwise NOT of a BSDFType, result cast back to BSDFType.
constexpr BSDFType operator~(BSDFType a) { return BSDFType(~uint32_t(a)); }

// Test a BSDFType field against a single lobe flag.
inline MaskP has_flag(const UInt32P& typeField, BSDFType flag) {
    return (typeField & UInt32P(uint32_t(flag))) != UInt32P(0u);
}

} // namespace lux2

#endif // LUX2_BSDF_TYPE_H
