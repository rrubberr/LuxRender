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

// Perturb the direction in which the emitter "sees" vertex h = n-2: rotate
// omega = normalize(h - e) about itself by an angle theta (log-uniform on
// [thetaMin, thetaMax]) and a uniform azimuth phi, trace from the emitter
// point e along the perturbed direction, and replace vertex n-2 with the hit.

#ifndef LUX2_INTEGRATORS_ERPT_CAUSTIC_H
#define LUX2_INTEGRATORS_ERPT_CAUSTIC_H

#include "integrators/erpt/mutator.h"

namespace lux2
{
    namespace erpt
    {
        class CausticMutator final : public Mutator
        {
        public:
            CausticMutator(float thetaMin = 1e-4f, float thetaMax = 0.1f)
                : m_tmin(thetaMin), m_tmax(thetaMax)
            {
            }

            MutationResult Propose(const WalkCtx &c, wf::PathStore &ps,
                                   const UInt32P &cur, const UInt32P &scr,
                                   const Int32P &n,
                                   const SpectrumWavelengthsP &sw,
                                   const MutRands &r,
                                   const MaskP &active) const override
            {
                MutationResult out;
                out.spanLo = n - Int32P(2);
                out.spanHi = n - Int32P(1);
                out.winLo = n - Int32P(3);
                out.winHi = n - Int32P(1);

                MaskP live = active && (n >= Int32P(3));
                if (none(live))
                    return out;

                const Int32P iH = n - Int32P(2), iE = n - Int32P(1),
                             iP = n - Int32P(3);
                const wf::VertexData E = wf::LoadVertex(c, ps, cur, iE, live);
                const Point3fP ph = PosOf(ps, cur, iH, live);
                const Point3fP pp = PosOf(ps, cur, iP, live);
                const wf::VertexData H = wf::LoadVertex(c, ps, cur, iH, live);

                Vector3fP dx = ph - E.p;
                const FloatP dx2 = dot(dx, dx);
                live = live && (dx2 > FloatP(EPS_DENOM));
                const Vector3fP om = dx * enoki::sqrt(divp(FloatP(1.f), dx2));

                const FloatP lr = FloatP(std::log(m_tmax / m_tmin));
                const FloatP theta = FloatP(m_tmax) * enoki::exp(-lr * r.u[0]);
                const FloatP phi = FloatP(2.f * float(M_PI)) * r.u[1];
                Vector3fP t1, t2;
                Frame(om, &t1, &t2);
                const FloatP st = enoki::sin(theta);
                const Vector3fP om2 =
                    om * enoki::cos(theta) +
                    (t1 * enoki::cos(phi) + t2 * enoki::sin(phi)) * st;

                // Same side of the emitter's geometric normal.
                const FloatP s0 = dot(om, E.dg.ng), s1 = dot(om2, E.dg.ng);
                live = live && (s0 * s1 > FloatP(0.f));

                const Point3fP o = OffsetRay(E.p, E.dg.ng, om2, FloatP(EPS_RAY));
                RayP ray = MutRay(o, om2, sw, live);
                HitP h;
                c.embree->Intersect(ray, h, Coherent::No);
                live = live && ChainableHit(c, h, live);
                if (none(live))
                    return out;

                // Reconnect new h' to the unchanged vertex n-3.
                Vector3fP dy = h.p - E.p;
                const FloatP dy2 = dot(dy, dy);
                Vector3fP back = pp - h.p;
                const FloatP lb = dot(back, back);
                live = live && (dy2 > FloatP(EPS_DENOM)) && (lb > FloatP(EPS_DENOM));
                if (none(live))
                    return out;
                const Vector3fP wbH = back * enoki::sqrt(divp(FloatP(1.f), lb));
                const wf::VertexGeomP g = HitToGeom(h, om2, wbH, live);
                StoreGeomVertex(ps, scr, iH, g, live);

                // Emitter's wb points at the new vertex.
                PatchWb(ps, scr, iE,
                        (h.p - E.p) * enoki::sqrt(divp(FloatP(1.f), dy2)), live);

                const FloatP cosx = abs(dot(om, H.dg.ng));
                const FloatP cosy = abs(dot(om2, h.ngeo));
                live = live && (cosy > FloatP(0.f)) && (cosx > FloatP(0.f));
                out.tRatio = select(live,
                                    (cosx * divp(FloatP(1.f), dx2)) *
                                        divp(dy2, cosy),
                                    FloatP(1.f));
                out.ok = live;
                return out;
            }

        private:
            float m_tmin, m_tmax;
        };

    } // namespace erpt
} // namespace lux2

#endif // LUX2_INTEGRATORS_ERPT_CAUSTIC_H
