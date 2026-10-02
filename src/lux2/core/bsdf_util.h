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

#ifndef LUX2_BSDF_UTIL_H
#define LUX2_BSDF_UTIL_H

#include "core/bsdf.h"
#include "core/math.h"

namespace lux2
{

    // BSDF ng side test where Dot(wi, ng) / Dot(wo, ng) and zeroed when
    // |Dot(wo, ng)| is grazing so the lane can be rejected.
    inline FloatP sideTest(const Vector3fP &wo, const Vector3fP &wi,
                           const DifferentialGeometryP &dg)
    {
        const FloatP cosWo = dot(wo, dg.ng);
        return select(abs(cosWo) < EPS_DENOM, FloatP(0.f),
                      dot(wi, dg.ng) / cosWo);
    }

    // The eye path (Radiance == reverse=true) must not apply the ng Jacobian
    // because this would double count the shading normal correction.
    inline FloatP modeJacobian(TransportMode mode, const FloatP &st)
    {
        // mode is scalar, so a plain branch is lane-legal.
        return mode == TransportMode::Importance ? abs(st) : FloatP(1.f);
    }

} // namespace lux2

#endif // LUX2_BSDF_UTIL_H
