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

// Perturb the raster position of the camera sub-path uniformly in a +-R px
// box, retrace the camera ray, and replace vertex 1 with the new first hit.
// Subsequent vertices are untouched.

#ifndef LUX2_INTEGRATORS_ERPT_LENS_H
#define LUX2_INTEGRATORS_ERPT_LENS_H

#include "integrators/erpt/mutator.h"

namespace lux2
{
    namespace erpt
    {
        class LensMutator final : public Mutator
        {
        public:
            explicit LensMutator(float radiusPx = 4.5f) : m_r(radiusPx) {}

            MutationResult Propose(const WalkCtx &c, wf::PathStore &ps,
                                   const UInt32P &cur, const UInt32P &scr,
                                   const Int32P &n,
                                   const SpectrumWavelengthsP &sw,
                                   const MutRands &r,
                                   const MaskP &active) const override
            {
                MutationResult out;
                out.spanLo = Int32P(1);
                out.spanHi = Int32P(2);
                out.winLo = Int32P(1);
                out.winHi = Int32P(2);
                out.tRatio = FloatP(1.f);

                // Current raster position of the camera subpath.
                FloatP px(0.f), py(0.f), We(0.f);
                const MaskP inFrame = PathPixel(c, ps, cur, &px, &py, &We, active);
                MaskP live = inFrame;
                if (none(live))
                    return out;

                const FloatP nx = px + (r.u[0] - FloatP(0.5f)) * FloatP(2.f * m_r);
                const FloatP ny = py + (r.u[1] - FloatP(0.5f)) * FloatP(2.f * m_r);

                RayP ray;
                FloatP w(0.f);
                c.camera->GenerateRay(nx, ny, FloatP(0.f), &ray, &w);
                ray.wavelengths = sw.w;
                ray.mask = UInt32P(0xFFFFFFFFu);
                ray.alive = live;

                HitP h;
                c.embree->Intersect(ray, h, Coherent::No);
                live = live && ChainableHit(c, h, live);
                if (none(live))
                    return out;

                // New raster position must stay in the frame!
                const Point3fP p0 = PosOf(ps, cur, Int32P(0), live);
                Vector3fP toHit = h.p - p0;
                const FloatP d2 = dot(toHit, toHit);
                live = live && (d2 > FloatP(EPS_DENOM));
                const Vector3fP wiCam = toHit * enoki::sqrt(divp(FloatP(1.f), d2));
                FloatP qx(0.f), qy(0.f), qWe(0.f), qCos(0.f);
                const MaskP inView =
                    c.camera->Project(wiCam, &qx, &qy, &qWe, &qCos, live);
                live = live && inView && (qWe > FloatP(0.f));
                if (none(live))
                    return out;

                // wb1 -> camera ; wb2 -> new vertex 1.
                const Vector3fP wb1 = -wiCam;
                const wf::VertexGeomP g = HitToGeom(h, ray.d, wb1, live);
                StoreGeomVertex(ps, scr, Int32P(1), g, live);

                const Point3fP p2 = PosOf(ps, cur, Int32P(2), live);
                Vector3fP d12 = h.p - p2;
                const FloatP l12 = dot(d12, d12);
                live = live && (l12 > FloatP(EPS_DENOM));
                PatchWb(ps, scr, Int32P(2),
                        d12 * enoki::sqrt(divp(FloatP(1.f), l12)), live);

                out.ok = live;
                return out;
            }

        private:
            float m_r;
        };

    } // namespace erpt
} // namespace lux2

#endif // LUX2_INTEGRATORS_ERPT_LENS_H
