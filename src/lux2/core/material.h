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

#ifndef LUX2_MATERIAL_H
#define LUX2_MATERIAL_H

#include "core/bsdf.h"
#include "core/bsdf_type.h"

namespace lux2 {

// Abstract material.
class Material {
public:
    virtual ~Material() = default;

    // Union of lobe types this BSDF can produce. The integrator
    // uses this to decide NEE and specular handling.
    virtual BSDFType flags() const = 0;

    // Return the BSDF to use at a shading point.
    virtual const BSDF* GetBSDF(const DifferentialGeometryP& dg) const = 0;
};

} // namespace lux2

#endif // LUX2_MATERIAL_H
