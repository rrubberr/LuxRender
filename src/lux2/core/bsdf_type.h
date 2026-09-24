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

#include "core/vecp.h"

#include <cstdint>

namespace lux2
{

    // BSDF lobe-type bitmask.
    enum class BSDFType : uint32_t
    {
        None = 0x00000,                 // no lobes
        Null = 0x00001,                 // no deflection
        DiffuseReflection = 0x00002,    // lambertian reflection
        DiffuseTransmission = 0x00004,  // lambertian transmission
        GlossyReflection = 0x00008,     // non-delta reflection lobe
        GlossyTransmission = 0x00010,   // non-delta transmission lobe.
        SpecularReflection = 0x00020,   // delta reflection
        SpecularTransmission = 0x00040, // delta transmission
        Delta1DReflection = 0x00080,    // reserved for rough glass/metal 1D lobes
        Delta1DTransmission = 0x00100,  // reserved
        Anisotropic = 0x01000,          // attribute: not rotation invariant about normal
        FrontSide = 0x08000,            // attribute: interacts on frontfacing side
        BackSide = 0x10000,             // attribute: interacts on backfacing side

        // Compound constants.
        Reflection = uint32_t(DiffuseReflection) | uint32_t(GlossyReflection) | uint32_t(SpecularReflection),
        Transmission = uint32_t(DiffuseTransmission) | uint32_t(GlossyTransmission) | uint32_t(SpecularTransmission) | uint32_t(Null),
        Diffuse = uint32_t(DiffuseReflection) | uint32_t(DiffuseTransmission),
        Glossy = uint32_t(GlossyReflection) | uint32_t(GlossyTransmission),
        Specular = uint32_t(SpecularReflection) | uint32_t(SpecularTransmission),
        Smooth = uint32_t(Diffuse) | uint32_t(Glossy),
        All = uint32_t(Diffuse) | uint32_t(Glossy) | uint32_t(Specular) | uint32_t(Null),
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
    template <typename UInt32>
    constexpr auto has_flag(UInt32 flags, BSDFType f)
    {
        return enoki::neq(flags & UInt32(uint32_t(f)), UInt32(0u));
    }

} // namespace lux2

#endif // LUX2_BSDF_TYPE_H
