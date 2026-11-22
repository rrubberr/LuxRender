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

#ifndef LUX2_SAMPLING_H
#define LUX2_SAMPLING_H

#include "core/math/geometry.h"
#include "core/math/math.h"
#include "core/math/vecp.h"

namespace lux2
{

    // Formerly LuxRays MC primitives (utils/mc.cpp).

    // Map (u1, u2) in [0,1)^2 to the unit disk.
    inline void concentricSampleDisk(const FloatP &u1, const FloatP &u2,
                                     FloatP *dx, FloatP *dy)
    {
        const FloatP sx = FloatP(2.f) * u1 - FloatP(1.f);
        const FloatP sy = FloatP(2.f) * u2 - FloatP(1.f);

        FloatP r, theta;
        const MaskP r1 = (sx >= -sy) && (sx > sy);   // region 1
        const MaskP r2 = (sx >= -sy) && !(sx > sy);  // region 2
        const MaskP r3 = !(sx >= -sy) && (sx <= sy); // region 3
        r = select(r1, sx, select(r2, sy, select(r3, -sx, -sy)));
        theta = select(r1,
                       select(sy > FloatP(0.f), sy / r,
                              FloatP(8.f) + sy / r),
                       select(r2, FloatP(2.f) - sx / r,
                              select(r3, FloatP(4.f) - sy / r,
                                     FloatP(6.f) + sx / r)));
        theta *= PI * FloatP(0.25f);

        // Degeneracy at the origin (was early return).
        const MaskP origin = eq(sx, FloatP(0.f)) && eq(sy, FloatP(0.f));
        enoki::masked(r, origin) = FloatP(0.f);
        enoki::masked(theta, origin) = FloatP(0.f);

        *dx = r * cos(theta);
        *dy = r * sin(theta);
    }

    // Acklam inverse normal CDF.
    inline FloatP normalCDFInverse(const FloatP &pIn)
    {
        constexpr float A1 = -3.969683028665376e+01f, A2 = 2.209460984245205e+02f,
                        A3 = -2.759285104469687e+02f, A4 = 1.383577518672690e+02f,
                        A5 = -3.066479806614716e+01f, A6 = 2.506628277459239e+00f;
        constexpr float B1 = -5.447609879822406e+01f, B2 = 1.615858368580409e+02f,
                        B3 = -1.556989798598866e+02f, B4 = 6.680131188771972e+01f,
                        B5 = -1.328068155288572e+01f;
        constexpr float C1 = -7.784894002430293e-03f, C2 = -3.223964580411365e-01f,
                        C3 = -2.400758277161838e+00f, C4 = -2.549732539343734e+00f,
                        C5 = 4.374664141464968e+00f, C6 = 2.938163982698783e+00f;
        constexpr float D1 = 7.784695709041462e-03f, D2 = 3.224671290700398e-01f,
                        D3 = 2.445134137142996e+00f, D4 = 3.754408661907416e+00f;
        constexpr float P_LOW = 0.02425f, P_HIGH = 0.97575f;

        const FloatP p = clamp(pIn, FloatP(1e-6f), FloatP(1.f - 1e-6f));
        const MaskP low = p < FloatP(P_LOW);
        const MaskP high = p > FloatP(P_HIGH);

        const FloatP ql = sqrt(FloatP(-2.f) * log(p));
        const FloatP rl = ((((C1 * ql + C2) * ql + C3) * ql + C4) * ql + C5) * ql + C6;
        const FloatP dl = (((D1 * ql + D2) * ql + D3) * ql + D4) * ql + FloatP(1.f);

        const FloatP qm = p - FloatP(0.5f);
        const FloatP rm = qm * qm;
        const FloatP num = ((((A1 * rm + A2) * rm + A3) * rm + A4) * rm + A5) * rm + A6;
        const FloatP den = ((((B1 * rm + B2) * rm + B3) * rm + B4) * rm + B5) * rm + FloatP(1.f);

        const FloatP qh = sqrt(FloatP(-2.f) * log(FloatP(1.f) - p));
        const FloatP rh = ((((C1 * qh + C2) * qh + C3) * qh + C4) * qh + C5) * qh + C6;
        const FloatP dh = (((D1 * qh + D2) * qh + D3) * qh + D4) * qh + FloatP(1.f);

        return select(low, rl / dl, select(high, -rh / dh, num * qm / den));
    }

    // Radial falloff profiles.
    inline FloatP gaussianSampleDisk(const FloatP &u1)
    {
        return clamp(normalCDFInverse(u1), FloatP(0.f), FloatP(1.f));
    }
    inline FloatP inverseGaussianSampleDisk(const FloatP &u1)
    {
        return clamp(FloatP(1.f) - normalCDFInverse(u1), FloatP(0.f), FloatP(1.f));
    }
    inline FloatP exponentialSampleDisk(const FloatP &u1, float power)
    {
        return clamp(-log(u1) / FloatP(power), FloatP(0.f), FloatP(1.f));
    }
    inline FloatP inverseExponentialSampleDisk(const FloatP &u1, float power)
    {
        return clamp(FloatP(1.f) + log(u1) / FloatP(power), FloatP(0.f), FloatP(1.f));
    }
    inline FloatP triangularSampleDisk(const FloatP &u1)
    {
        const FloatP lo = sqrt(u1 * FloatP(0.5f));
        const FloatP hi = FloatP(1.f) - sqrt((FloatP(1.f) - u1) * FloatP(0.5f));
        return clamp(select(u1 <= FloatP(0.5f), lo, hi), FloatP(0.f), FloatP(1.f));
    }

    // Uniform sample of a regular ngon with circumradius 1.
    inline void samplePolygon(const FloatP &u1, const FloatP &u2, int blades,
                              FloatP *dx, FloatP *dy)
    {
        const float nf = float(blades);
        const FloatP e = u2 * FloatP(nf);
        const Int32P i = floor2int<Int32P>(min(e, FloatP(nf - 1.f)));
        const FloatP f = e - FloatP(i); // position along the edge, uniform

        const FloatP a0 = (2.f * PI / float(blades)) * FloatP(i);
        const FloatP a1 = (2.f * PI / float(blades)) * FloatP(i + Int32P(1));
        const Vector3fP v0(cos(a0), sin(a0), FloatP(0.f));
        const Vector3fP v1(cos(a1), sin(a1), FloatP(0.f));

        // Uniform barycentric in triangle (origin, v0, v1).
        const FloatP su = sqrt(u1);
        const FloatP w1 = su * (FloatP(1.f) - f);
        const FloatP w2 = su * f;
        const Vector3fP p = v0 * w1 + v1 * w2;
        *dx = p.x();
        *dy = p.y();
    }

    // Map two uniforms in [0,1)^2 onto the unit aperture. blades < 3 selects
    // the circular disk (concentric, uniform).
    inline void sampleLens(const FloatP &u1, const FloatP &u2, int blades,
                           int distribution, int power, FloatP *dx, FloatP *dy)
    {
        (void)distribution;
        (void)power;
        if (blades < 3)
            concentricSampleDisk(u1, u2, dx, dy);
        else
            samplePolygon(u1, u2, blades, dx, dy);
    }

    // Cosine sample the hemisphere around the unit axis n.
    inline Vector3fP cosineSampleHemisphere(const Normal3fP &n,
                                            const Vector3fP &wo,
                                            const FloatP &u1,
                                            const FloatP &u2)
    {
        FloatP px, py;
        concentricSampleDisk(u1, u2, &px, &py);
        const FloatP pz = sqrt(max(FloatP(1.f) - px * px - py * py,
                                   FloatP(0.f)));
        const auto basis = coordinate_system(Vector3fP(n.x(), n.y(), n.z()));
        const Vector3fP nn(n.x(), n.y(), n.z());
        Vector3fP wi(fmadd(basis.first, px,
                           fmadd(basis.second, py, nn * pz)));
        enoki::masked(wi, dot(wo, n) < FloatP(0.f)) = -wi;
        return wi;
    }

    // Was SameHemisphere (w.z * wp.z > 0) about an arbitrary axis.
    inline MaskP sameHemisphere(const Vector3fP &w, const Vector3fP &wp,
                                const Normal3fP &n)
    {
        return dot(w, n) * dot(wp, n) > FloatP(0.f);
    }

} // namespace lux2

#endif // LUX2_SAMPLING_H
