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

#ifndef LUX2_LIGHT_H
#define LUX2_LIGHT_H

// Abstract light source interface for NEE and photon emission.

#include "core/vecp.h"
#include "core/geometry.h"
#include "core/spectrum.h"
#include "core/bsdf.h"
#include "core/bsdf_type.h"
#include "core/ray.h"

namespace lux2 {

// Abstract light source.
class Light {
public:
    virtual ~Light() = default;

    // Lobe types this light emits into. Used for MIS weighting.
    virtual BSDFType flags() const = 0;

    // Infinite lights have no finite sample position.
    virtual bool IsInfinite() const = 0;

    // Light group index.
    virtual UInt group() const = 0;

    // Radiance emitted along `ray` (used for infinite light and
    // area-light).
    virtual SWCSpectrumP Le(const RayP& ray, MaskP active = MaskP(true)) const = 0;

    // NEE / photon-emission position sampler.
    virtual MaskP Sample_L(const Point3fP& p, const Normal3fP& n,
                           const FloatP& u0, const FloatP& u1,
                           Point3fP* lightP, Vector3fP* wi,
                           FloatP* pdf, SWCSpectrumP* Le,
                           MaskP active = MaskP(true)) const = 0;

    // PDF of sampling direction wi from p. Used for MIS.
    virtual FloatP Pdf_L(const Point3fP& p, const Normal3fP& n,
                         const Vector3fP& wi,
                         MaskP active = MaskP(true)) const = 0;
};

} // namespace lux2

#endif // LUX2_LIGHT_H
