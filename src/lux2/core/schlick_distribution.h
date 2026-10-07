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

#ifndef LUX2_SCHLICK_DISTRIBUTION_H
#define LUX2_SCHLICK_DISTRIBUTION_H

#include "core/vecp.h"
#include "core/geometry.h"
#include "core/math.h"

namespace lux2
{

    class SchlickDistribution
    {
    public:
        SchlickDistribution(const FloatP &roughness, const FloatP &anisotropy)
            : m_roughness(roughness), m_anisotropy(anisotropy) {}

        // Sample a half-vector (local frame, z == n). d == pdf == D(wh).
        void SampleH(const FloatP &u1, const FloatP &u2,
                     Vector3fP *wh, FloatP *d, FloatP *pdf) const;

        FloatP D(const Vector3fP &wh) const
        {
            return SchlickZ(abs(wh.z())) * SchlickA(wh) * INVPI;
        }

        FloatP Pdf(const Vector3fP &wh) const { return D(wh); }

        FloatP G(const Vector3fP &wo, const Vector3fP &wi,
                 const Vector3fP &) const
        {
            return SchlickG(abs(wo.z())) * SchlickG(abs(wi.z()));
        }

    private:
        FloatP SchlickG(const FloatP &costheta) const
        {
            return costheta /
                   fmadd(costheta, FloatP(1.f) - m_roughness, m_roughness);
        }

        FloatP SchlickZ(const FloatP &cosNH) const
        {
            const FloatP cosNH2 = cosNH * cosNH;
            const FloatP d = fmadd(cosNH2, m_roughness, FloatP(1.f) - cosNH2);
            // (r/d)/d avoids overflow in d*d.
            return (m_roughness / d) / d;
        }

        FloatP SchlickA(const Vector3fP &H) const
        {
            const FloatP h = sqrt(sqr(H.x()) + sqr(H.y()));
            const MaskP nonzero = h > FloatP(0.f);
            const FloatP hSafe = select(nonzero, h, FloatP(1.f));
            const FloatP w = select(m_anisotropy > FloatP(0.f), H.x(), H.y()) / hSafe;
            const FloatP p = FloatP(1.f) - abs(m_anisotropy);
            const FloatP a =
                sqrt(p / fmadd(w * w, FloatP(1.f) - p * p, p * p));
            return select(nonzero, a, FloatP(1.f));
        }

        FloatP m_roughness;
        FloatP m_anisotropy;
    };

} // namespace lux2

#endif // LUX2_SCHLICK_DISTRIBUTION_H
