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

// ERPT full-path evaluation ORACLE (debug reference).
//
// Relocated out of integrators/erpt/path_eval.h: the production hot path uses
// EvalCanonWindow (canon_path.h) over a mutation's changed span. These whole-
// path forms re-trace every interior edge for visibility -- Embree-heavy, so
// they are NOT used in rendering. They remain here as the numeric reference
// for validating the windowed evaluator and, when specular chains land,
// specular evaluation:
//
//     Window(y,W)/Window(x,W) == EvalPathF(y)/EvalPathF(x)   (any valid W)
//     f / rrProd == Lc * p_s                                  (vs connector)
//
// EvalVertexTerm/EvalEdgeTerm live in path_eval.h and are shared with the hot
// path; this header only adds the whole-path assembly on top of them.

#ifndef LUX2_TESTS_ERPT_PATH_EVAL_ORACLE_H
#define LUX2_TESTS_ERPT_PATH_EVAL_ORACLE_H

#include "integrators/erpt/path_eval.h"
#include "integrators/wavefront/pool_view.h"
#include "integrators/wavefront/storage.h"

namespace lux2
{
    namespace erpt
    {
        // Bare measurement contribution f (area measure) of a connected
        // bidirectional path: eye vertices 0..nE-1 (0 = camera) joined at the
        // tip to light vertices 0..nL-1 (0 = emitter). Includes We (camWeight)
        // and Le. Every edge -- including the camera edge (0,1) and the light
        // edge (0,1) -- is evaluated; the join is bare 1/d^2.
        inline SWCSpectrumP EvalPathF(const WalkCtx &c, const PathStore &eyePs,
                                      const PathStore &lPath_store,
                                      const UInt32P &ePath, const UInt32P &lPath,
                                      const Int32P &nE, const Int32P &nL,
                                      const SpectrumWavelengthsP &sw,
                                      const FloatP &camWeight,
                                      const MaskP &active, MaskP *ok)
        {
            const PathStore &lightPs = lPath_store;
            MaskP live = active && (nE >= Int32P(2)) && (nL >= Int32P(1));
            SWCSpectrumP f(camWeight);

            // ---- Emission at the light origin (vertex 0) ------------------
            {
                const wf::VertexData L0 =
                    wf::LoadVertex(c, lightPs, lPath, Int32P(0), live);
                const LightPtr selLight = enoki::gather<LightPtr>(
                    c.lightPtrs.data(), wf::GatherSelIdx(lightPs, lPath, live),
                    live);
                const Point3fP aimTip =
                    wf::LoadPosition(eyePs, ePath, nE - Int32P(1), live && eq(nL, Int32P(1)));
                const Point3fP aimNext =
                    wf::LoadPosition(lightPs, lPath, Int32P(1), live && (nL > Int32P(1)));
                const Point3fP aim = select(nL > Int32P(1), aimNext, aimTip);
                const Vector3fP dRaw = aim - L0.p;
                const FloatP d2 = dot(dRaw, dRaw);
                const Vector3fP wo = dRaw * enoki::sqrt(divp(FloatP(1.f), d2));
                f = f * selLight->Le(L0.dg, sw, wo, live);
            }

            // ---- Light edge 0 -> 1 (emitter to first scatter) -------------
            {
                const MaskP seg = live && (nL > Int32P(1));
                if (any(seg))
                {
                    const wf::VertexData V0 =
                        wf::LoadVertex(c, lightPs, lPath, Int32P(0), seg);
                    const wf::VertexData V1 =
                        wf::LoadVertex(c, lightPs, lPath, Int32P(1), seg);
                    const Vector3fP dRaw = V1.p - V0.p;
                    const FloatP d2 = dot(dRaw, dRaw);
                    const Vector3fP wi =
                        dRaw * enoki::sqrt(divp(FloatP(1.f), d2));
                    const MaskP pre = seg && (d2 > FloatP(EPS_DENOM));
                    if (any(pre))
                    {
                        const ShadowRng rng{UInt32P(0u), UInt32P(0u), Int32P(0),
                                            Int32P(0), UInt32P(0u), 0u};
                        const EdgeTerm et = EvalEdgeTerm(
                            c.shadow, V0.p, V0.dg.ng, wi, V1.p, V1.dg.ng, V1.isMed,
                            /*farCos=*/true, sw, wf::StartMedium(V0, wi), rng, pre);
                        f = f * et.g * et.Tr;
                        live = live && (!pre || et.ok);
                    }
                }
            }

            // ---- Light interior vertices 1..nL-2 --------------------------
            for (int i = MAX_DEPTH - 2; i >= 1; --i)
            {
                const Int32P vi = Int32P(i);
                const MaskP seg = live && (vi <= nL - Int32P(2));
                if (!any(seg))
                    continue;
                const wf::VertexData V =
                    wf::LoadVertex(c, lightPs, lPath, vi, seg);
                const wf::VertexData Vn =
                    wf::LoadVertex(c, lightPs, lPath, vi + Int32P(1), seg);
                const Vector3fP dRaw = Vn.p - V.p;
                const FloatP d2 = dot(dRaw, dRaw);
                const Vector3fP wi =
                    dRaw * enoki::sqrt(divp(FloatP(1.f), d2));

                const VertexTerm vt =
                    EvalVertexTerm(V, sw, wi, TransportMode::Importance, seg);
                f = f * vt.f;
                live = live && (!seg || vt.ok);

                const MaskP pre = seg && live && (d2 > FloatP(EPS_DENOM));
                if (!any(pre))
                    continue;
                const ShadowRng rng{UInt32P(0u), UInt32P(0u), vi, Int32P(0),
                                    UInt32P(0u), 0u};
                const EdgeTerm et = EvalEdgeTerm(
                    c.shadow, V.p, V.dg.ng, wi, Vn.p, Vn.dg.ng, Vn.isMed,
                    /*farCos=*/true, sw, wf::StartMedium(V, wi), rng, pre);
                f = f * et.g * et.Tr;
                live = live && (!pre || et.ok);
            }

            // ---- Connection segment: eye tip <-> light tip ----------------
            {
                const wf::VertexData E =
                    wf::LoadVertex(c, eyePs, ePath, nE - Int32P(1), live);
                const wf::VertexData L0 =
                    wf::LoadVertex(c, lightPs, lPath, nL - Int32P(1), live);
                const Vector3fP dRaw = L0.p - E.p;
                const FloatP d2 = dot(dRaw, dRaw);
                const Vector3fP ewi =
                    dRaw * enoki::sqrt(divp(FloatP(1.f), d2));
                const Vector3fP lwi = -ewi;

                const VertexTerm et =
                    EvalVertexTerm(E, sw, ewi, TransportMode::Radiance, live);
                const MaskP isOrigin = eq(nL, Int32P(1));
                const VertexTerm lt = EvalVertexTerm(
                    L0, sw, lwi, TransportMode::Importance, live && !isOrigin);
                const wf::EmitVertexData Lz =
                    wf::LoadEmitVertex(lightPs, lPath, Int32P(0), live && isOrigin);
                const FloatP front = dot(lwi, Lz.dg.ng);
                const FloatP oLfv = select(front > FloatP(0.f),
                                           abs(dot(lwi, Lz.dg.n)), FloatP(0.f));
                const SWCSpectrumP ltipF =
                    select(isOrigin, SWCSpectrumP(oLfv), lt.f);

                f = f * et.f * ltipF;
                live = live && (!isOrigin || (oLfv > FloatP(0.f))) &&
                       (!(!isOrigin) || lt.ok) && (!live || et.ok);

                const MaskP pre =
                    live && (d2 > FloatP(EPS_DENOM)) && (f > SWCSpectrumP(0.f));
                if (!any(pre))
                {
                    *ok = MaskP(false);
                    return SWCSpectrumP(0.f);
                }
                const ShadowRng rng{UInt32P(0u), UInt32P(0u), Int32P(0),
                                    Int32P(0), UInt32P(0u), 0u};
                const EdgeTerm ct = EvalEdgeTerm(
                    c.shadow, E.p, E.dg.ng, ewi, L0.p, L0.dg.ng, L0.isMed,
                    /*farCos=*/false, sw, wf::StartMedium(E, ewi), rng, pre);
                f = f * ct.g * ct.Tr;
                live = live && (!pre || ct.ok);
            }

            // ---- Eye interior vertices 1..nE-2 ----------------------------
            for (int i = MAX_DEPTH - 2; i >= 1; --i)
            {
                const Int32P vi = Int32P(i);
                const MaskP seg = live && (vi <= nE - Int32P(2));
                if (!any(seg))
                    continue;
                const wf::VertexData V =
                    wf::LoadVertex(c, eyePs, ePath, vi, seg);
                const wf::VertexData Vn =
                    wf::LoadVertex(c, eyePs, ePath, vi + Int32P(1), seg);
                const Vector3fP dRaw = Vn.p - V.p;
                const FloatP d2 = dot(dRaw, dRaw);
                const Vector3fP wi =
                    dRaw * enoki::sqrt(divp(FloatP(1.f), d2));

                const VertexTerm vt =
                    EvalVertexTerm(V, sw, wi, TransportMode::Radiance, seg);
                f = f * vt.f;
                live = live && (!seg || vt.ok);

                const MaskP pre = seg && live && (d2 > FloatP(EPS_DENOM));
                if (!any(pre))
                    continue;
                const ShadowRng rng{UInt32P(0u), UInt32P(0u), vi, Int32P(0),
                                    UInt32P(0u), 0u};
                const EdgeTerm et = EvalEdgeTerm(
                    c.shadow, V.p, V.dg.ng, wi, Vn.p, Vn.dg.ng, Vn.isMed,
                    /*farCos=*/true, sw, wf::StartMedium(V, wi), rng, pre);
                f = f * et.g * et.Tr;
                live = live && (!pre || et.ok);
            }

            // ---- Camera edge 0 -> 1 ---------------------------------------
            {
                const MaskP seg = live && (nE > Int32P(1));
                if (any(seg))
                {
                    const wf::VertexData V0 =
                        wf::LoadVertex(c, eyePs, ePath, Int32P(0), seg);
                    const wf::VertexData V1 =
                        wf::LoadVertex(c, eyePs, ePath, Int32P(1), seg);
                    const Vector3fP dRaw = V1.p - V0.p;
                    const FloatP d2 = dot(dRaw, dRaw);
                    const Vector3fP wi =
                        dRaw * enoki::sqrt(divp(FloatP(1.f), d2));
                    const MaskP pre = seg && (d2 > FloatP(EPS_DENOM));
                    if (any(pre))
                    {
                        const ShadowRng rng{UInt32P(0u), UInt32P(0u), Int32P(0),
                                            Int32P(0), UInt32P(0u), 0u};
                        const EdgeTerm et = EvalEdgeTerm(
                            c.shadow, V0.p, V0.dg.ng, wi, V1.p, V1.dg.ng, V1.isMed,
                            /*farCos=*/true, sw, wf::StartMedium(V0, wi), rng, pre);
                        f = f * et.g * et.Tr;
                        live = live && (!pre || et.ok);
                    }
                }
            }

            *ok = live;
            return select(*ok, f, SWCSpectrumP(0.f));
        }

        // ---- Relative (hot-path) form: changed-span cancellation ----------
        struct SpanWindow
        {
            Int32P eLo, eHi, lLo, lHi;
            MaskP camEdge, lightEdge01;
            MaskP conn;
        };

        inline SWCSpectrumP EvalPathFWindow(const WalkCtx &c,
                                            const PathStore &eyePs,
                                            const PathStore &lPath_store,
                                            const UInt32P &ePath,
                                            const UInt32P &lPath,
                                            const Int32P &nE, const Int32P &nL,
                                            const SpectrumWavelengthsP &sw,
                                            const FloatP &camWeight,
                                            const SpanWindow &win,
                                            const MaskP &active, MaskP *ok)
        {
            const PathStore &lightPs = lPath_store;
            MaskP live = active && (nE >= Int32P(2)) && (nL >= Int32P(1));
            SWCSpectrumP f(camWeight);

            const MaskP winL0 = live && (win.lLo <= Int32P(0)) && (win.lHi >= Int32P(0));
            if (any(winL0))
            {
                const wf::VertexData L0 =
                    wf::LoadVertex(c, lightPs, lPath, Int32P(0), winL0);
                const LightPtr selLight = enoki::gather<LightPtr>(
                    c.lightPtrs.data(), wf::GatherSelIdx(lightPs, lPath, winL0),
                    winL0);
                const Point3fP aimTip =
                    wf::LoadPosition(eyePs, ePath, nE - Int32P(1), winL0 && eq(nL, Int32P(1)));
                const Point3fP aimNext =
                    wf::LoadPosition(lightPs, lPath, Int32P(1), winL0 && (nL > Int32P(1)));
                const Point3fP aim = select(nL > Int32P(1), aimNext, aimTip);
                const Vector3fP dRaw = aim - L0.p;
                const FloatP d2 = dot(dRaw, dRaw);
                const Vector3fP wo = dRaw * enoki::sqrt(divp(FloatP(1.f), d2));
                f = f * selLight->Le(L0.dg, sw, wo, winL0);
            }

            if (any(live && win.lightEdge01 && (nL > Int32P(1))))
            {
                const MaskP seg = live && win.lightEdge01 && (nL > Int32P(1));
                const wf::VertexData V0 =
                    wf::LoadVertex(c, lightPs, lPath, Int32P(0), seg);
                const wf::VertexData V1 =
                    wf::LoadVertex(c, lightPs, lPath, Int32P(1), seg);
                const Vector3fP dRaw = V1.p - V0.p;
                const FloatP d2 = dot(dRaw, dRaw);
                const Vector3fP wi = dRaw * enoki::sqrt(divp(FloatP(1.f), d2));
                const MaskP pre = seg && (d2 > FloatP(EPS_DENOM));
                if (any(pre))
                {
                    const ShadowRng rng{UInt32P(0u), UInt32P(0u), Int32P(0),
                                        Int32P(0), UInt32P(0u), 0u};
                    const EdgeTerm et = EvalEdgeTerm(
                        c.shadow, V0.p, V0.dg.ng, wi, V1.p, V1.dg.ng, V1.isMed,
                        true, sw, wf::StartMedium(V0, wi), rng, pre);
                    f = f * et.g * et.Tr;
                    live = live && (!pre || et.ok);
                }
            }

            for (int i = MAX_DEPTH - 2; i >= 1; --i)
            {
                const Int32P vi = Int32P(i);
                const MaskP seg = live && (vi <= nL - Int32P(2)) &&
                                  (vi >= win.lLo) && (vi <= win.lHi);
                if (!any(seg))
                    continue;
                const wf::VertexData V =
                    wf::LoadVertex(c, lightPs, lPath, vi, seg);
                const wf::VertexData Vn =
                    wf::LoadVertex(c, lightPs, lPath, vi + Int32P(1), seg);
                const Vector3fP dRaw = Vn.p - V.p;
                const FloatP d2 = dot(dRaw, dRaw);
                const Vector3fP wi = dRaw * enoki::sqrt(divp(FloatP(1.f), d2));
                const VertexTerm vt =
                    EvalVertexTerm(V, sw, wi, TransportMode::Importance, seg);
                f = f * vt.f;
                live = live && (!seg || vt.ok);
                const MaskP pre = seg && live && (d2 > FloatP(EPS_DENOM));
                if (!any(pre))
                    continue;
                const ShadowRng rng{UInt32P(0u), UInt32P(0u), vi, Int32P(0),
                                    UInt32P(0u), 0u};
                const EdgeTerm et = EvalEdgeTerm(
                    c.shadow, V.p, V.dg.ng, wi, Vn.p, Vn.dg.ng, Vn.isMed, true, sw,
                    wf::StartMedium(V, wi), rng, pre);
                f = f * et.g * et.Tr;
                live = live && (!pre || et.ok);
            }

            if (any(live && win.conn))
            {
                const MaskP seg = live && win.conn;
                const wf::VertexData E =
                    wf::LoadVertex(c, eyePs, ePath, nE - Int32P(1), seg);
                const wf::VertexData L0 =
                    wf::LoadVertex(c, lightPs, lPath, nL - Int32P(1), seg);
                const Vector3fP dRaw = L0.p - E.p;
                const FloatP d2 = dot(dRaw, dRaw);
                const Vector3fP ewi = dRaw * enoki::sqrt(divp(FloatP(1.f), d2));
                const Vector3fP lwi = -ewi;
                const VertexTerm et =
                    EvalVertexTerm(E, sw, ewi, TransportMode::Radiance, seg);
                const MaskP isOrigin = eq(nL, Int32P(1));
                const VertexTerm lt = EvalVertexTerm(
                    L0, sw, lwi, TransportMode::Importance, seg && !isOrigin);
                const wf::EmitVertexData Lz =
                    wf::LoadEmitVertex(lightPs, lPath, Int32P(0), seg && isOrigin);
                const FloatP front = dot(lwi, Lz.dg.ng);
                const FloatP oLfv = select(front > FloatP(0.f),
                                           abs(dot(lwi, Lz.dg.n)), FloatP(0.f));
                const SWCSpectrumP ltipF =
                    select(isOrigin, SWCSpectrumP(oLfv), lt.f);
                f = f * et.f * ltipF;
                live = live && (!isOrigin || (oLfv > FloatP(0.f))) &&
                       (!(!isOrigin) || lt.ok) && (!seg || et.ok);
                const MaskP pre =
                    live && win.conn && (d2 > FloatP(EPS_DENOM)) &&
                    (f > SWCSpectrumP(0.f));
                if (any(pre))
                {
                    const ShadowRng rng{UInt32P(0u), UInt32P(0u), Int32P(0),
                                        Int32P(0), UInt32P(0u), 0u};
                    const EdgeTerm ct = EvalEdgeTerm(
                        c.shadow, E.p, E.dg.ng, ewi, L0.p, L0.dg.ng, L0.isMed,
                        false, sw, wf::StartMedium(E, ewi), rng, pre);
                    f = f * ct.g * ct.Tr;
                    live = live && (!pre || ct.ok);
                }
            }

            for (int i = MAX_DEPTH - 2; i >= 1; --i)
            {
                const Int32P vi = Int32P(i);
                const MaskP seg = live && (vi <= nE - Int32P(2)) &&
                                  (vi >= win.eLo) && (vi <= win.eHi);
                if (!any(seg))
                    continue;
                const wf::VertexData V =
                    wf::LoadVertex(c, eyePs, ePath, vi, seg);
                const wf::VertexData Vn =
                    wf::LoadVertex(c, eyePs, ePath, vi + Int32P(1), seg);
                const Vector3fP dRaw = Vn.p - V.p;
                const FloatP d2 = dot(dRaw, dRaw);
                const Vector3fP wi = dRaw * enoki::sqrt(divp(FloatP(1.f), d2));
                const VertexTerm vt =
                    EvalVertexTerm(V, sw, wi, TransportMode::Radiance, seg);
                f = f * vt.f;
                live = live && (!seg || vt.ok);
                const MaskP pre = seg && live && (d2 > FloatP(EPS_DENOM));
                if (!any(pre))
                    continue;
                const ShadowRng rng{UInt32P(0u), UInt32P(0u), vi, Int32P(0),
                                    UInt32P(0u), 0u};
                const EdgeTerm et = EvalEdgeTerm(
                    c.shadow, V.p, V.dg.ng, wi, Vn.p, Vn.dg.ng, Vn.isMed, true, sw,
                    wf::StartMedium(V, wi), rng, pre);
                f = f * et.g * et.Tr;
                live = live && (!pre || et.ok);
            }

            if (any(live && win.camEdge && (nE > Int32P(1))))
            {
                const MaskP seg = live && win.camEdge && (nE > Int32P(1));
                const wf::VertexData V0 =
                    wf::LoadVertex(c, eyePs, ePath, Int32P(0), seg);
                const wf::VertexData V1 =
                    wf::LoadVertex(c, eyePs, ePath, Int32P(1), seg);
                const Vector3fP dRaw = V1.p - V0.p;
                const FloatP d2 = dot(dRaw, dRaw);
                const Vector3fP wi = dRaw * enoki::sqrt(divp(FloatP(1.f), d2));
                const MaskP pre = seg && (d2 > FloatP(EPS_DENOM));
                if (any(pre))
                {
                    const ShadowRng rng{UInt32P(0u), UInt32P(0u), Int32P(0),
                                        Int32P(0), UInt32P(0u), 0u};
                    const EdgeTerm et = EvalEdgeTerm(
                        c.shadow, V0.p, V0.dg.ng, wi, V1.p, V1.dg.ng, V1.isMed,
                        true, sw, wf::StartMedium(V0, wi), rng, pre);
                    f = f * et.g * et.Tr;
                    live = live && (!pre || et.ok);
                }
            }

            *ok = live;
            return select(*ok, f, SWCSpectrumP(0.f));
        }

        // Product of Russian-roulette compensation factors actually applied to
        // a stored path's flux. Used ONLY by the debug oracle; acceptance never
        // touches rr. `rrStart` must match the walker's RR_START.
        inline FloatP RRProduct(const PathStore &ps, const UInt32P &path,
                                const Int32P &n, bool eyeSide, int rrStart,
                                const MaskP &active)
        {
            FloatP prod(1.f);
            const uint32_t *base = ps.arena->misWords.data();
            for (int i = 1; i < MAX_DEPTH; ++i)
            {
                const Int32P vi = Int32P(i);
                const MaskP seg = active && (vi < n) &&
                                  ((vi + Int32P(1)) > Int32P(rrStart));
                if (!any(seg))
                    continue;
                const UInt32P rec = wf::RecIdx(ps, path, vi, seg);
                const uint32_t word = eyeSide ? wf::kWord_rrR : wf::kWord_rr;
                const FloatP r = reinterpret_array<FloatP>(
                    wf::LoadWord(base, wf::RecWord<wf::MIS_WORDS>(rec, word),
                                 seg));
                prod = select(seg, prod * r, prod);
            }
            return prod;
        }

    } // namespace erpt
} // namespace lux2

#endif // LUX2_TESTS_ERPT_PATH_EVAL_ORACLE_H
