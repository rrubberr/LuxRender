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

// Mutation strategy interface for ERPT chains.

// Proposes a new path y from the chain's current path x.

#ifndef LUX2_INTEGRATORS_ERPT_MUTATOR_H
#define LUX2_INTEGRATORS_ERPT_MUTATOR_H

#include "integrators/erpt/canon_path.h"
#include "integrators/erpt/chainable.h"

#include "core/accel/embree2.h"
#include "core/bsdf.h"
#include "core/bsdf_type.h"
#include "core/bsdfptr_table.h"
#include "integrators/wavefront/lane/advance.h"
#include "integrators/wavefront/lane/geom.h"

namespace lux2
{
    namespace erpt
    {
        // Random numbers handed to a mutator in [0,1).
        struct MutRands
        {
            FloatP u[2];
        };

        struct MutationResult
        {
            MaskP ok;              // geometrically valid proposal was written
            Int32P spanLo, spanHi; // vertex records written (resync span), incl. wb patches
            Int32P winLo, winHi;   // evaluator window (vertex terms)
            Int32P firstEdge;      // edges with index < firstEdge drop their
                                   // geometric g (Jacobian cancelled by the
                                   // mutation); Tr and the degenerate guard
                                   // stay. 0 = drop nothing.
            FloatP tRatio;         // T(y->x) / T(x->y), area measure
            MutationResult()
                : ok(false), spanLo(0), spanHi(0), winLo(0), winHi(0),
                  firstEdge(0), tRatio(1.f)
            {
            }
        };

        class Mutator
        {
        public:
            virtual ~Mutator() = default;
            // ps holds both slots; cur == scratch contents on entry.
            virtual MutationResult Propose(const WalkCtx &c, wf::PathStore &ps,
                                           const UInt32P &cur, const UInt32P &scr,
                                           const Int32P &n,
                                           const SpectrumWavelengthsP &sw,
                                           const MutRands &r,
                                           const MaskP &active) const = 0;
        };

        // =========================================================================
        // Shared mutator helpers
        // =========================================================================

        // Overwrite `wb` of vertex v.
        inline void PatchWb(wf::PathStore &ps, const UInt32P &path, const Int32P &v,
                            const Vector3fP &wb, const MaskP &m)
        {
            const UInt32P rec = wf::RecIdx(ps, path, v, m);
            uint32_t *base = ps.arena->geomWords.data();
            wf::StoreWord(base, enoki::reinterpret_array<UInt32P>(wb.x()),
                          wf::RecWord<wf::GEOM_WORDS>(rec, wf::kWord_wb), m);
            wf::StoreWord(base, enoki::reinterpret_array<UInt32P>(wb.y()),
                          wf::RecWord<wf::GEOM_WORDS>(rec, wf::kWord_wb + 1), m);
            wf::StoreWord(base, enoki::reinterpret_array<UInt32P>(wb.z()),
                          wf::RecWord<wf::GEOM_WORDS>(rec, wf::kWord_wb + 2), m);
        }

        // Overwrite a whole vertex record.
        inline void StoreGeomVertex(wf::PathStore &ps, const UInt32P &path,
                                    const Int32P &v, const wf::VertexGeomP &g,
                                    const MaskP &m)
        {
            const UInt32P rec = wf::RecIdx(ps, path, v, m);
            wf::StoreGeom(ps.arena->geomWords.data(), g, rec, m);
        }

        // Position of vertex v.
        inline Point3fP PosOf(const wf::PathStore &ps, const UInt32P &path,
                              const Int32P &v, const MaskP &m)
        {
            return wf::LoadPosition(ps, path, v, m);
        }

        // Orthonormal frame around unit n.
        inline void Frame(const Vector3fP &n, Vector3fP *t1, Vector3fP *t2)
        {
            const FloatP sign = enoki::copysign(FloatP(1.f), n.z());
            const FloatP a = FloatP(-1.f) * divp(FloatP(1.f), sign + n.z());
            const FloatP b = n.x() * n.y() * a;
            *t1 = Vector3fP(FloatP(1.f) + sign * n.x() * n.x() * a, sign * b,
                            -sign * n.x());
            *t2 = Vector3fP(b, sign + n.y() * n.y() * a, -n.y());
        }

        // Ray for a mutator trace.
        inline RayP MutRay(const Point3fP &o, const Vector3fP &d,
                           const SpectrumWavelengthsP &sw, const MaskP &m)
        {
            RayP r;
            r.o = o;
            r.d = d;
            r.mint = FloatP(0.f);
            r.maxt = FloatP(INF_F);
            r.time = FloatP(0.f);
            r.medium = UInt32P(LUX2_MEDIUM_CLEAR);
            r.wavelengths = sw.w;
            r.mask = UInt32P(0xFFFFFFFFu);
            r.alive = m;
            return r;
        }

        // Trace from `ray` to the first non-Null surface, collapsing Null
        // crossings.
        inline MaskP TraceSolid(const WalkCtx &c, RayP &ray, HitP &hit)
        {
            MaskP tracing = ray.alive;
            MaskP landed(false);
            HitP best;
            best.hit = MaskP(false);
            for (int k = 0; k <= MAX_NULL_CROSSINGS && any(tracing); ++k)
            {
                ray.alive = tracing;
                HitP h;
                c.embree->Intersect(ray, h, Coherent::No);
                const MaskP seen = tracing && h.hit;

                MaskP isNull(false);
                if (k < MAX_NULL_CROSSINGS && any(seen))
                {
                    const BSDFPtr b = c.table->Gather(h.matID, seen);
                    isNull = seen && has_flag(b->flags(seen), BSDFType::Null);
                }

                // A lane that is not Null this iteration terminates on `h`.
                const MaskP done = tracing && !isNull;
                landed = landed || done;
                best = SelectHit(done, h, best);

                // Offset through a Null boundary and swap to its exit medium.
                ray.o = select(isNull,
                               OffsetRay(h.p, h.ngeo, ray.d, FloatP(EPS_RAY)),
                               ray.o);
                ray.maxt = select(isNull, FloatP(INF_F), ray.maxt);
                if (c.hasMedia && any(isNull))
                    ray.medium = select(isNull, SwapMedium(h, ray.d), ray.medium);
                tracing = isNull;
            }
            hit = best;
            return landed && best.hit;
        }

        // Build the geom record of a surface hit as a scatter vertex whose
        // predecessor is in direction `wb` from the hit.
        inline wf::VertexGeomP HitToGeom(const HitP &h, const Vector3fP &rayD,
                                         const Vector3fP &wb, const MaskP &m)
        {
            const DifferentialGeometryP dg = MakeDG(h, rayD, m);
            wf::VertexGeomP g;
            g.p = h.p;
            g.n = dg.n;
            g.ng = dg.ng;
            g.wb = wb;
            g.dp_du = h.dp_du;
            g.dp_dv = h.dp_dv;
            g.dp_ds = h.dp_ds;
            g.dp_dt = h.dp_dt;
            g.uv = h.uv;
            g.flux = FloatP(0.f);
            g.k = FloatP(0.f);
            g.footprint = FloatP(0.f);
            g.spare0 = FloatP(0.f);
            g.matID = h.matID;
            g.medID = UInt32P(LUX2_MEDIUM_CLEAR);
            g.medFront = h.mediumFrontID;
            g.medBack = h.mediumBackID;
            return g;
        }

    } // namespace erpt
} // namespace lux2

#endif // LUX2_INTEGRATORS_ERPT_MUTATOR_H
