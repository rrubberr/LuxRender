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

#include "core/schlick_distribution.h"

#include "core/math.h"

namespace lux2
{

    namespace
    {

        // GetPhi(a, b) = (pi/2) * sqrt(a*b / (1 - a*(1 - b))).
        inline FloatP GetPhi(const FloatP &a, const FloatP &b)
        {
            return FloatP(0.5f) * PI * sqrt(a * b / (FloatP(1.f) - a * (FloatP(1.f) - b)));
        }

    } // namespace

    void SchlickDistribution::SampleH(const FloatP &u1, const FloatP &u2in,
                                      Vector3fP *wh, FloatP *d, FloatP *pdf) const
    {
        const FloatP u2 = u2in * FloatP(4.f);

        const FloatP cos2Theta = u1 / (m_roughness * (FloatP(1.f) - u1) + u1);
        const FloatP cosTheta = sqrt(max(FloatP(0.f), cos2Theta));
        const FloatP sinTheta = sqrt(max(FloatP(0.f), FloatP(1.f) - cos2Theta));
        const FloatP p = FloatP(1.f) - abs(m_anisotropy);

        // Four quadrants selected by the u2 band.
        const FloatP q0 = GetPhi(sqr(u2), sqr(p));
        const FloatP t1 = FloatP(2.f) - u2;
        const FloatP q1 = FloatP(PI) - GetPhi(sqr(t1), sqr(p));
        const FloatP t2 = u2 - FloatP(2.f);
        const FloatP q2 = FloatP(PI) + GetPhi(sqr(t2), sqr(p));
        const FloatP t3 = FloatP(4.f) - u2;
        const FloatP q3 = FloatP(2.f) * PI - GetPhi(sqr(t3), sqr(p));

        FloatP phi = select(u2 < FloatP(1.f), q0,
                            select(u2 < FloatP(2.f), q1,
                                   select(u2 < FloatP(3.f), q2, q3)));

        phi = phi + select(m_anisotropy > FloatP(0.f), FloatP(0.5f) * PI, FloatP(0.f));

        *wh = Vector3fP(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);

        const FloatP D = SchlickZ(cosTheta) * SchlickA(*wh) * INVPI;
        *d = D;
        *pdf = D;
    }

} // namespace lux2
