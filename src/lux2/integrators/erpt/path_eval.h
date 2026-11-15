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

// Direct path evaluator for mutation-based integrators (ERPT).
//
// MEASURE: AREA. f is the bare measurement contribution written entirely in
// surface-area measure, so it depends only on the path geometry -- never on
// where the eye/light split falls, nor on which side drove the sampling. That
// split-invariance is what lets the evaluator be checked against itself (the
// every-split test) and against the connector's Lc*p_s.
//
// In area measure every edge carries BOTH endpoint cosines over d^2. We
// distribute them so each vertex term holds exactly one cosine (its own
// outgoing side, which is what BSDF::Eval already folds -- ev.f =
// f_r*|cos_out|) and each edge carries the FAR endpoint's incoming cosine:
//
//     f = We * Le * PROD(vertex term) * PROD(edge term)
//       vertex term (BSDF v)  = f_r(wi,wo) * |cos_out|        (= ev.f)
//       vertex term (camera)  = We   (sensor response, passed in)
//       vertex term (emitter) = Le(dg, sw, wo)
//       edge term (p0 -> p1)  = |cos at p1 toward p0| / d^2 * Tr
//       join edge (tip<->tip) = bare 1/d^2 * Tr   (both cosines already sit
//                              in the two tip vertex terms -- see below)
//
// Why the join is bare: the eye-tip vertex term carries |cos(eyeTip->lightTip)|
// and the light-tip term carries |cos(lightTip->eyeTip)| (oLfv when the tip is
// the emitter), matching GenConnector exactly
// (connectors.h: Lc = E.flux * ef.f * lfv * L0.flux * invD2 * Tr). Interior
// edges instead take the far cosine because the near cosine already lives in
// that vertex's own ev.f.
//
// SPECULAR / DELTA ENDS (deferred): a delta lobe is not a free variable, so a
// product of G terms across a specular chain is only exact for unfolded flat
// mirrors; it misses a curvature term. The agreed plan is to land area measure
// and its tests DIFFUSE-ONLY first, then handle specular chains as a separate
// step whose curvature correction lives in the mutator's Q (the density of the
// propagated perturbation at the first diffuse vertex, converted to area).
// When that lands, a delta end of an edge must drop its cosine (the delta's
// 1/|cos| cancels it) -- see EvalEdgeTerm's farCos note.
//
// ERPT acceptance is a deterministic function of geometry:
//     a = min(1, f(y) / f(x))
// so f is NEVER read from the stored per-vertex `flux` (that is the sampled
// throughput with Russian-roulette compensation baked in and no geometric
// term), nor from rr/rrR or the MIS words. EvalPathF re-evaluates f from
// geometry alone, so a given path always presents the same f on revisit.
//
// The hot-path relative form (EvalPathFWindow) reuses these same two term
// functions over a changed span; correctness is anchored by the full product
// here and by the every-split invariance test.

#ifndef LUX2_INTEGRATORS_ERPT_PATH_EVAL_H
#define LUX2_INTEGRATORS_ERPT_PATH_EVAL_H

#include "integrators/core/vertex.h"
#include "integrators/lane/direct.h"
#include "integrators/lane/math.h"
#include "integrators/wf/pool_view.h"
#include "integrators/wf/storage.h"
#include "integrators/wf/types.h"

// connectors.h is not self-contained; it needs these first (as bidirectional.cpp
// provides) for the Film/Camera types its CamAbaSink and camera connector use.
#include "core/camera.h"
#include "core/film.h"
#include "core/light.h"

#include "integrators/wf/connectors.h"

#include <enoki/array.h>

namespace lux2
{
    namespace erpt
    {
        using wf::PathStore;

        // A vertex's BSDF/phase term toward direction `wi`, given the stored
        // vertex data. Returns f*|cos| (the folded BSDF value) and reports
        // whether the vertex is a valid participant. Delta/specular lobes
        // return a valid f per the BSDF::Eval contract, so this handles
        // specular interiors without a separate reflectance path (their cosine
        // handling is refined when specular chains land -- see header).
        struct VertexTerm
        {
            SWCSpectrumP f; // BSDF f * cos (or phase value)
            FloatP cosGeo;  // |dot(wi, ng)| at this vertex (geometric normal)
            MaskP ok;
        };

        inline VertexTerm EvalVertexTerm(const wf::VertexData &V,
                                         const SpectrumWavelengthsP &sw,
                                         const Vector3fP &wi, TransportMode mode,
                                         const MaskP &active)
        {
            VertexTerm r;
            r.f = SWCSpectrumP(0.f);
            r.cosGeo = FloatP(0.f);
            r.ok = MaskP(false);

            VertexP vx;
            vx.valid = active;
            vx.isMedium = V.isMed;
            vx.p = V.p;
            vx.dg = V.dg;
            vx.wo = V.wb;
            vx.k = V.k;
            vx.bsdf = V.bsdf;
            vx.med = V.med;

            BSDFEvalP ev = ZeroEval();
            FloatP cosT(0.f);
            EvalVertex(vx, sw, wi, &ev, &cosT, active);
            r.f = ev.f;
            // Geometric-normal cosine for the edge term (matches the walker's
            // cB/cN which use ng; the BSDF's own cos inside ev.f uses n).
            r.cosGeo = select(V.isMed, FloatP(1.f), abs(dot(wi, V.dg.ng)));
            r.ok = active && (ev.f > SWCSpectrumP(0.f));
            return r;
        }

        // Edge term between two positions, in AREA measure:
        //     |cos at p1 toward p0| / d^2 * Tr
        // The near cosine (at p0) is NOT here -- it lives in p0's own vertex
        // term (ev.f). The far cosine belongs to p1 and is applied on the edge
        // because p1's own vertex term carries its OTHER side.
        //
        // farCos: interior edges pass true (far cosine included). The join
        // edge passes false -- both tip cosines already sit in the two tip
        // vertex terms, so the join is a bare 1/d^2 * Tr (matching the
        // connector). When specular chains land, an edge whose far end is a
        // delta lobe must also pass farCos=false: the delta's implicit
        // 1/|cos| cancels this cosine.
        struct EdgeTerm
        {
            FloatP g;   // |cosFar| / d^2  (or bare 1/d^2 when !farCos)
            SWCSpectrumP Tr;
            MaskP ok;
        };

        inline EdgeTerm EvalEdgeTerm(const ShadowCtx &sc, const Point3fP &p0,
                                     const Normal3fP &ng0, const Vector3fP &dir01,
                                     const Point3fP &p1, const Normal3fP &ng1,
                                     const MaskP &p1IsMed, bool farCos,
                                     const SpectrumWavelengthsP &sw,
                                     const UInt32P &startMed,
                                     const ShadowRng &rng, const MaskP &active)
        {
            EdgeTerm r;
            r.g = FloatP(0.f);
            r.Tr = SWCSpectrumP(0.f);
            r.ok = MaskP(false);

            const Vector3fP dRaw = p1 - p0;
            const FloatP d2 = dot(dRaw, dRaw);
            const MaskP pre = active && (d2 > FloatP(EPS_DENOM));
            if (!any(pre))
                return r;

            // Area-measure geometry: far cosine over d^2. Medium far ends have
            // no surface normal, so their cosine is 1.
            const FloatP cosFar =
                farCos ? select(p1IsMed, FloatP(1.f), abs(dot(dir01, ng1)))
                       : FloatP(1.f);
            r.g = cosFar * divp(FloatP(1.f), d2);

            const FloatP dist = enoki::sqrt(d2);
            const FloatP tmax =
                dist - enoki::max(FloatP(1e-3f), FloatP(1e-3f) * dist);
            const Point3fP so = OffsetRay(p0, ng0, dir01, FloatP(EPS_RAY));
            const Occlusion oc =
                Occlude(sc, so, dir01, startMed, tmax, sw, rng, pre);
            r.Tr = oc.Tr;
            r.ok = oc.ok && (r.g > FloatP(0.f));
            return r;
        }

        // Bare measurement contribution f (area measure) of a connected
        // bidirectional path: eye vertices 0..nE-1 (0 = camera) joined at the
        // tip to light vertices 0..nL-1 (0 = emitter). Includes We (camWeight)
        // and Le. Every edge -- including the camera edge (0,1) and the light
        // edge (0,1) -- is evaluated; the join is bare 1/d^2.
        //
        // NOTE: this re-traces every interior edge for visibility. That is
        // correct but Embree-heavy -- acceptable for a debug oracle, not the
        // hot path (the hot path only re-traces a mutation's changed span).
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
            // nL == 1: emitter connects straight to the eye tip; Le is
            // evaluated along the direction toward the eye tip.
            // nL  > 1: Le is evaluated toward light vertex 1, and edge 0->1
            // (with its far cosine at vertex 1) is applied just below.
            {
                const wf::VertexData L0 =
                    wf::LoadVertex(c, lightPs, lPath, Int32P(0), live);
                // lightIdx indexes lightPtrs directly. (An earlier revision
                // double-gathered through lsD->pSel, which is the FLOAT
                // selection-probability table -- it reinterpreted probability
                // bits as a light index.)
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
            // Interior in every split with nL >= 2: far cosine at vertex 1.
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
            // Vertex i looks toward vertex i+1 (toward the eye side). Edge
            // i->i+1 carries the far cosine at i+1.
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
            // Bare 1/d^2: the eye-tip term carries |cos(eyeTip->lightTip)| and
            // the light-tip term (or oLfv at the emitter) carries the other.
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
            // Vertex i looks toward vertex i+1. Edge i->i+1 carries the far
            // cosine at i+1.
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
            // The camera (vertex 0) has no BSDF; its contribution is We
            // (camWeight, already applied). This edge still carries area
            // geometry: the far cosine at vertex 1 toward the camera over d^2.
            // Validated separately against the walker's dARW[1] =
            // camPdf * cos(v1) / d^2 (the identity camWeight bypasses).
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
        //
        // f is a pure product of per-vertex and per-edge factors, so for a
        // mutation the acceptance ratio f(y)/f(x) collapses to the product of
        // the CHANGED factors: every factor whose inputs are untouched appears
        // identically in numerator and denominator and cancels exactly.
        // EvalPathFWindow evaluates the same term functions as EvalPathF but
        // multiplies only factors whose index lies inside a caller-supplied
        // window; feeding it path y and path x and dividing yields the ratio
        // while re-tracing only the window's edges.
        //
        // The window MUST be a superset of the changed factors. A mutation
        // that moves vertices [l..m] changes: those vertices' terms, the edges
        // touching them (l-1->l and m->m+1), AND the two boundary vertices'
        // own terms -- vertex l-1's wi now points at the new m-side neighbour
        // and vertex m+1's wo (stored wb) still points at the OLD vertex m
        // until the mutator patches it. So a correct window is
        // [l-1 .. m+1] on the affected side, not [l..m]. Correctness is
        // anchored by the test identity
        //     Window(y,W)/Window(x,W) == EvalPathF(y)/EvalPathF(x)
        // for any valid W, and == 1 when W is empty (no-op mutation).
        struct SpanWindow
        {
            // Inclusive vertex-index ranges on each side; eLo>eHi disables the
            // eye interior, lLo>lHi disables the light interior.
            Int32P eLo, eHi, lLo, lHi;
            // Include the camera edge (0,1) and the light edge (0,1).
            MaskP camEdge, lightEdge01;
            // Include the connection segment (eye tip <-> light tip) and the
            // endpoint vertex terms / emission that live with it.
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

            // Emission at light vertex 0, only if the light window covers it.
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

            // Light edge 0 -> 1.
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

            // Light interior vertices in [max(1,lLo), min(nL-2,lHi)].
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

            // Connection segment (eye tip <-> light tip) and its endpoint
            // vertex terms / origin cosine, gated by win.conn.
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

            // Eye interior vertices in [max(1,eLo), min(nE-2,eHi)].
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

            // Camera edge 0 -> 1.
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
        // a stored path's flux. Eye vertices divide beta by rrR[v]; light
        // vertices divide by rr[v]; both only where (v+1) > RR_START.
        //
        // The walker does `beta = beta * f; beta *= 1/rrR` (see
        // bidirectional_consumer.h OnVertex), so flux carries a factor 1/rrProd
        // and the connector's Lc = f / (p_s * rrProd). Rearranged, the oracle
        // identity is
        //     f / rrProd == Lc * p_s      (equivalently f == Lc * p_s * rrProd)
        // NOT f * rrProd == Lc * p_s. Used ONLY by the debug oracle; acceptance
        // never touches rr. `rrStart` must match the walker's RR_START.
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

#endif // LUX2_INTEGRATORS_ERPT_PATH_EVAL_H
