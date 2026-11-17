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

// Pool read primitive for wavefront connectors.

#ifndef LUX2_INTEGRATORS_WF_POOL_VIEW_H
#define LUX2_INTEGRATORS_WF_POOL_VIEW_H

#include "integrators/core/vertex.h"
#include "integrators/debug/integrator_debug.h"
#include "integrators/wavefront/storage.h"
#include "integrators/wavefront/types.h"

#include "core/bsdfptr_table.h"
#include "core/mediumptr_table.h"

#include <enoki/array.h>

namespace lux2
{
    namespace wf
    {
        // One vertex, gathered from the pool for the whole packet.
        struct VertexData
        {
            Point3fP p;
            Vector3fP wb;
            DifferentialGeometryP dg;
            BSDFPtr bsdf;
            MediumPtr med;
            SWCSpectrumP flux;
            FloatP k;
            FloatP dAW, dARW, rr, rrR, cosi, coso, d2;
            MaskP spec, isMed, recvHasN;
            UInt32P medID, medFrontID, medBackID;
        };

        // Three MIS words for the previous vertex term.
        struct MisPair
        {
            FloatP cosi, coso, d2;
        };
        inline MisPair LoadMisPair(const PathStore &ps, const UInt32P &path,
                                   const Int32P &idx, const MaskP &act)
        {
            const uint32_t *base = ps.arena->misWords.data();
            const UInt32P rec = RecIdx(ps, path, idx, act);
            MisPair r;
            r.cosi = LoadField<MisTag::cosi>(base, rec, act);
            r.coso = LoadField<MisTag::coso>(base, rec, act);
            r.d2 = LoadField<MisTag::d2>(base, rec, act);
            return r;
        }

        // Emitter vertex 0 needs position and normals only.
        struct EmitVertexData
        {
            Point3fP p;
            DifferentialGeometryP dg;
        };

        inline EmitVertexData LoadEmitVertex(const PathStore &ps,
                                             const UInt32P &path,
                                             const Int32P &idx,
                                             const MaskP &act)
        {
            const uint32_t *base = ps.arena->geomWords.data();
            const UInt32P rec = RecIdx(ps, path, idx, act);
            EmitVertexData v;
            v.p = LoadField<GeomTag::p>(base, rec, act);
            v.dg.p = v.p;
            v.dg.n = LoadField<GeomTag::n>(base, rec, act);
            v.dg.ng = LoadField<GeomTag::ng>(base, rec, act);
            return v;
        }

        // Position of vertex `idx` of `path` only.
        inline Point3fP LoadPosition(const PathStore &ps, const UInt32P &path,
                                     const Int32P &idx, const MaskP &act)
        {
            const UInt32P rec = RecIdx(ps, path, idx, act);
            const uint32_t *base = ps.arena->geomWords.data();
            return LoadField<GeomTag::p>(base, rec, act);
        }

        // Gather vertex `idx` of `path` for the lanes where `act` holds.
        // `site` attributes the call in debug stats only.
        inline VertexData LoadVertex(const WalkCtx &c, const PathStore &ps,
                                     const UInt32P &path, const Int32P &idx,
                                     const MaskP &act,
                                     WfLoadSite site = WF_LOAD_OTHER)
        {
            WfStatLoad(site, enoki::count(act));
            const UInt32P rec = RecIdx(ps, path, idx, act);
            const VertexGeomP g = LoadGeom(ps.arena->geomWords.data(), rec, act);
            const VertexMisP mv = LoadMis(ps.arena->misWords.data(), rec, act);

            VertexData v;
            v.p = g.p;
            v.wb = g.wb;
            v.flux = g.flux;
            v.k = g.k;

            v.dg.p = g.p;
            v.dg.n = g.n;
            v.dg.ng = g.ng;
            // Compute entering.
            v.dg.entering = dot(g.wb, g.ng) > FloatP(0.f);
            v.dg.uv_u = g.uv.x();
            v.dg.uv_v = g.uv.y();
            v.dg.dp_du = g.dp_du;
            v.dg.dp_dv = g.dp_dv;
            v.dg.dp_ds = g.dp_ds;
            v.dg.dp_dt = g.dp_dt;
            v.dg.footprint = g.footprint;

            // Surface if not a medium scatter.
            v.isMed = neq(g.medID, UInt32P(LUX2_MEDIUM_CLEAR)) && act;
            const MaskP surf = act && !v.isMed;
            v.bsdf = c.table->Gather(g.matID, surf);
            const MaskP inMed =
                c.mediumTable->GatherNonVacuum(g.medID, act) && act;
            v.med = c.mediumTable->Gather(g.medID, inMed);

            v.dAW = mv.dAW;
            v.dARW = mv.dARW;
            v.rr = mv.rr;
            v.rrR = mv.rrR;
            v.cosi = mv.cosi;
            v.coso = mv.coso;
            v.d2 = mv.d2;
            v.spec = neq(mv.bits & UInt32P(1u), UInt32P(0u)) && act;
            v.medID = g.medID;
            v.medFrontID = g.medFront;
            v.medBackID = g.medBack;

            // recvHasN from the gathered BSDF.
            v.recvHasN = RecvHasNormal(surf, v.bsdf);
            return v;
        }

 #ifdef LUX2_INTEGRATOR_DEBUG
        // Gather the per light vertex flux-luma / matID / specular-bit chain
        // for firefly diagnosis.
        inline LightChainSnap LoadDiagLightChain(const PathStore &ps,
                                                 const UInt32P &path,
                                                 const Int32P &len,
                                                 const MaskP &act)
        {
            LightChainSnap snap;
            snap.len = len;
            const uint32_t *gb = ps.arena->geomWords.data();
            const uint32_t *mb = ps.arena->misWords.data();
            const uint32_t *db = ps.arena->diagWords.data();
            for (int v = 0; v < LightChainSnap::kMax; ++v)
            {
                const MaskP vm = act && (Int32P(v) < len);
                const UInt32P rec = RecIdx(ps, path, Int32P(v), vm);
                snap.cosi[v] = reinterpret_array<FloatP>(
                    LoadWord(db, RecWord<DIAG_WORDS>(rec, 0u), vm));
                snap.cost[v] = reinterpret_array<FloatP>(
                    LoadWord(db, RecWord<DIAG_WORDS>(rec, 1u), vm));
                snap.nng[v] = reinterpret_array<FloatP>(
                    LoadWord(db, RecWord<DIAG_WORDS>(rec, 2u), vm));
                snap.st[v] = reinterpret_array<FloatP>(
                    LoadWord(db, RecWord<DIAG_WORDS>(rec, 3u), vm));
                snap.wsel[v] = reinterpret_array<FloatP>(
                    LoadWord(db, RecWord<DIAG_WORDS>(rec, 4u), vm));
                snap.fval[v] = reinterpret_array<FloatP>(
                    LoadWord(db, RecWord<DIAG_WORDS>(rec, 5u), vm));
                // flux is a single scalar throughput word; read it directly.
                snap.flux[v] =
                    select(vm, LoadField<GeomTag::flux>(gb, rec, vm),
                           FloatP(0.f));
                snap.matID[v] =
                    LoadWord(gb, RecWord<GEOM_WORDS>(rec, GeomTag::matID::Word), vm) &
                    UInt32P(0xFFFFu);
                snap.spec[v] =
                    LoadWord(mb, RecWord<MIS_WORDS>(rec, MisTag::bits::Word), vm) &
                    UInt32P(1u);
            }
            return snap;
        }
#endif

        // Medium a ray leaves vertex v into, given the leaving direction.
        inline UInt32P StartMedium(const VertexData &v, const Vector3fP &dir)
        {
            const MaskP front = dot(dir, v.dg.ng) > FloatP(0.f);
            const UInt32P surfMed = select(front, v.medFrontID, v.medBackID);
            return select(v.isMed, v.medID, surfMed);
        }

    } // namespace wf
} // namespace lux2

#endif // LUX2_INTEGRATORS_WF_POOL_VIEW_H
