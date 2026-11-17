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

// Eye ordered chain paths.

#ifndef LUX2_INTEGRATORS_ERPT_CANON_PATH_H
#define LUX2_INTEGRATORS_ERPT_CANON_PATH_H

#include "integrators/wavefront/lane/math.h"
#include "integrators/erpt/path_eval.h"
#include "integrators/erpt/chainable.h"
#include "integrators/wavefront/pool_view.h"
#include "integrators/wavefront/seed_sink.h"
#include "integrators/wavefront/storage.h"

#include "core/camera.h"
#include "core/light.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace lux2
{
    namespace erpt
    {
        using wf::GEOM_WORDS;

        // Evaluator window span (vertex terms in [lo,hi], edges i->i+1 for
        // lo <= i < hi).
        inline const uint32_t *GeomRec(const wf::PathStore &ps, uint32_t path,
                                       uint32_t v)
        {
            return ps.arena->geomWords.data() +
                   (size_t(path) * ps.arena->stride + v) * GEOM_WORDS;
        }
        inline uint32_t *GeomRecMut(wf::PathStore &ps, uint32_t path, uint32_t v)
        {
            return ps.arena->geomWords.data() +
                   (size_t(path) * ps.arena->stride + v) * GEOM_WORDS;
        }
        inline uint32_t MisBits(const wf::PathStore &ps, uint32_t path, uint32_t v)
        {
            return ps.arena->misWords
                .data()[(size_t(path) * ps.arena->stride + v) * wf::MIS_WORDS +
                        wf::kWord_bits];
        }
        // Specular flag for a vertex.
        inline bool VertexIsSpecular(const uint32_t *geomRec)
        {
            return (geomRec[wf::kWord_spare0] & 1u) != 0u;
        }

        // Chain side vertex load.
        inline wf::VertexData LoadChainVertex(const WalkCtx &c,
                                              const wf::PathStore &ps,
                                              const UInt32P &path,
                                              const Int32P &idx, const MaskP &act)
        {
            wf::VertexData v = wf::LoadVertex(c, ps, path, idx, act);
            const UInt32P rec = wf::RecIdx(ps, path, idx, act);
            const UInt32P sp0 = enoki::gather<UInt32P>(
                ps.arena->geomWords.data(),
                wf::RecWord<wf::GEOM_WORDS>(rec, wf::kWord_spare0), act);
            v.spec = neq(sp0 & UInt32P(1u), UInt32P(0u)) && act;
            return v;
        }
        inline float WordF(const uint32_t *rec, uint32_t w)
        {
            float f;
            std::memcpy(&f, &rec[w], sizeof(f));
            return f;
        }
        inline void SetWordF(uint32_t *rec, uint32_t w, float f)
        {
            std::memcpy(&rec[w], &f, sizeof(f));
        }
        inline void SetV3(uint32_t *rec, uint32_t w, float x, float y, float z)
        {
            SetWordF(rec, w, x);
            SetWordF(rec, w + 1, y);
            SetWordF(rec, w + 2, z);
        }
        inline uint32_t PackPair(uint32_t lo, uint32_t hi)
        {
            return (lo & 0xFFFFu) | (hi << 16u);
        }

        // One lane of a SeedRec extracted to scalars.
        struct SeedLane
        {
            wf::SeedKind kind = wf::SeedKind::Gen;
            const wf::PathStore *eyePs = nullptr;
            uint32_t ePath = 0u, nE = 0u;
            const wf::PathStore *lightPs = nullptr;
            uint32_t lPath = 0u, nL = 0u;
            float vP[3] = {0, 0, 0}, vN[3] = {0, 0, 0}, vNg[3] = {0, 0, 0};
            uint32_t vLightIdx = 0u, vAreaId = 0u;
        };

        struct CanonInfo
        {
            uint32_t n = 0u;        // vertex count
            uint32_t lightIdx = 0u; // emitter, index into lightPtrs
            // Specular run boundaries.
            uint32_t kE = 0u, kL = 0u;
        };

        // Resolve an area light id to its lightPtrs index.
        inline bool AreaToLightIdx(const WalkCtx &c, uint32_t areaId, uint32_t *idx)
        {
            if (areaId >= c.areaPtrs.size())
                return false;
            const Light *l = c.areaPtrs[areaId];
            for (size_t i = 0; i < c.lightPtrs.size(); ++i)
                if (c.lightPtrs[i] == l)
                {
                    *idx = uint32_t(i);
                    return true;
                }
            return false;
        }

        // Convert one seed lane to eye ordered geom words (n * GEOM_WORDS).
        inline bool BuildCanonicalSeed(const WalkCtx &c, const SeedLane &L,
                                       std::vector<uint32_t> &out, CanonInfo *info)
        {
            using namespace wf;
            // Null is allowed!
            if (c.hasMedia)
                return false;

            const bool realLight =
                (L.kind == SeedKind::Gen || L.kind == SeedKind::Cam);
            const uint32_t nEs = (L.kind == SeedKind::Cam ||
                                  L.kind == SeedKind::CamNee)
                                     ? 1u
                                     : L.nE;
            const uint32_t nLs = realLight ? L.nL : 1u;
            if (nEs < 1u || nLs < 1u)
                return false;
            const uint32_t n = nEs + nLs;
            if (n < 3u)
                return false; // directly visible emitter (n == 2): class C

            // Interior vertices must be surfaces.
            auto interiorOk = [&](const PathStore &ps, uint32_t path, uint32_t lo,
                                  uint32_t hiExcl)
            {
                for (uint32_t v = lo; v < hiExcl; ++v)
                {
                    const uint32_t medW = GeomRec(ps, path, v)[kWord_matID];
                    if ((medW >> 16u) != uint32_t(LUX2_MEDIUM_CLEAR))
                        return false; // medium scatter
                }
                return true;
            };
            if (L.kind != SeedKind::Cam && L.kind != SeedKind::CamNee)
                if (nEs > 2u && !interiorOk(*L.eyePs, L.ePath, 1u, nEs - 1u))
                    return false; // eye interior = 1 .. nEs-2
            if (realLight && nLs > 2u &&
                !interiorOk(*L.lightPs, L.lPath, 1u, nLs - 1u))
                return false; // light interior = 1 .. nLs-2

            // Emitter identity.
            uint32_t lightIdx = 0u;
            if (realLight)
                lightIdx = L.lightPs->lightIdx[L.lPath];
            else if (L.kind == SeedKind::HitLight)
            {
                if (!AreaToLightIdx(c, L.vAreaId, &lightIdx))
                    return false;
            }
            else
                lightIdx = L.vLightIdx;
            if (lightIdx >= c.lightPtrs.size() || c.lightPtrs[lightIdx] == nullptr ||
                c.lightPtrs[lightIdx]->IsInfinite())
                return false;

            // Build.
            out.assign(size_t(n) * GEOM_WORDS, 0u);
            auto rec = [&](uint32_t v)
            { return out.data() + size_t(v) * GEOM_WORDS; };

            // Eye part, as stored.
            for (uint32_t v = 0; v < nEs; ++v)
                std::memcpy(rec(v), GeomRec(*L.eyePs, L.ePath, v),
                            sizeof(uint32_t) * GEOM_WORDS);

            auto setWbToPrev = [&](uint32_t v) -> bool
            {
                const uint32_t *prev = rec(v - 1u);
                uint32_t *cur = rec(v);
                const float dx = WordF(prev, kWord_p) - WordF(cur, kWord_p);
                const float dy = WordF(prev, kWord_p + 1) - WordF(cur, kWord_p + 1);
                const float dz = WordF(prev, kWord_p + 2) - WordF(cur, kWord_p + 2);
                const float d2 = dx * dx + dy * dy + dz * dz;
                if (!(d2 > EPS_DENOM))
                    return false;
                const float inv = 1.f / std::sqrt(d2);
                SetV3(cur, kWord_wb, dx * inv, dy * inv, dz * inv);
                return true;
            };

            if (realLight)
            {
                // light vertex j -> Eye ordered index nEs + (nLs-1-j).
                for (uint32_t j = nLs; j-- > 0u;)
                {
                    const uint32_t dst = nEs + (nLs - 1u - j);
                    std::memcpy(rec(dst), GeomRec(*L.lightPs, L.lPath, j),
                                sizeof(uint32_t) * GEOM_WORDS);
                    if (!setWbToPrev(dst))
                        return false;
                }
            }
            else
            {
                uint32_t *e = rec(nEs);
                SetV3(e, kWord_p, L.vP[0], L.vP[1], L.vP[2]);
                SetV3(e, kWord_n, L.vN[0], L.vN[1], L.vN[2]);
                SetV3(e, kWord_ng, L.vNg[0], L.vNg[1], L.vNg[2]);
                e[kWord_matID] = PackPair(0u, uint32_t(LUX2_MEDIUM_CLEAR));
                const uint32_t med = lightIdx < c.lightMedium.size()
                                         ? c.lightMedium[lightIdx]
                                         : uint32_t(LUX2_MEDIUM_CLEAR);
                e[kWord_medFront] = PackPair(med, med);
                if (!setWbToPrev(nEs))
                    return false;
            }

            // Stamp the specular bit into each copied vertex's spare0 bit0.
            auto matDelta = [&](uint32_t v) -> bool
            {
                const uint32_t matID = rec(v)[kWord_matID] & 0xFFFFu;
                if (matID >= c.table->ptrs.size())
                    return false;
                const BSDF *b = c.table->ptrs[matID];
                return b && (b->flags() & uint32_t(BSDFType::Specular)) != 0u;
            };
            // Eye interior [1, nEs-1) from MIS bits; eye tip nEs-1 by material.
            for (uint32_t v = 1; v + 1u < nEs; ++v)
                if (MisBits(*L.eyePs, L.ePath, v) & 1u)
                    rec(v)[wf::kWord_spare0] |= 1u;
            if (nEs >= 2u && matDelta(nEs - 1u))
                rec(nEs - 1u)[wf::kWord_spare0] |= 1u;
            // Light interior [1, nLs-1) from MIS bits; light tip nLs-1.
            if (realLight)
            {
                for (uint32_t j = 1; j + 1u < nLs; ++j)
                    if (MisBits(*L.lightPs, L.lPath, j) & 1u)
                        rec(nEs + (nLs - 1u - j))[wf::kWord_spare0] |= 1u;
                if (nLs >= 2u && matDelta(nEs))
                    rec(nEs)[wf::kWord_spare0] |= 1u;
            }

            // Specular run structure.
            auto spec = [&](uint32_t v) -> bool
            { return VertexIsSpecular(rec(v)); };
            uint32_t kE = 0u;
            while (kE + 1u <= n - 2u && spec(kE + 1u))
                ++kE;
            uint32_t kL = n - 1u;
            while (kL >= 1u + 1u && kL - 1u >= kE + 1u && spec(kL - 1u))
                --kL;
            // No specular may sit in the diffuse gap (kE+1 .. kL-1).
            for (uint32_t v = kE + 1u; v <= kL - 1u && v <= n - 2u; ++v)
                if (spec(v))
                    return false;
            // At least one diffuse vertex strictly between the runs.
            if (!(kE + 1u <= kL - 1u))
                return false;

            // The mutators perturb the diffuse endpoints, lens at kE+1,
            // caustic at kL-1. Both must be chainable surface scatters.
            auto mutableOk = [&](uint32_t v) -> bool
            {
                const uint32_t *r = rec(v);
                const uint32_t matID = r[kWord_matID] & 0xFFFFu;
                const uint32_t medID = r[kWord_matID] >> 16u;
                if (medID != uint32_t(LUX2_MEDIUM_CLEAR))
                    return false; // TODO: medium scatter not yet supported.
                return ChainableMat(c, matID);
            };
            if (!mutableOk(kE + 1u) || !mutableOk(kL - 1u))
                return false;

            info->n = n;
            info->lightIdx = lightIdx;
            info->kE = kE;
            info->kL = kL;
            return true;
        }

        // Raster position of an eye ordered path.
        inline MaskP PathPixel(const WalkCtx &c, const wf::PathStore &ps,
                               const UInt32P &path, FloatP *px, FloatP *py,
                               FloatP *We, const MaskP &active)
        {
            const Point3fP p0 = wf::LoadPosition(ps, path, Int32P(0), active);
            const Point3fP p1 = wf::LoadPosition(ps, path, Int32P(1), active);
            const Vector3fP d = p1 - p0;
            const FloatP d2 = dot(d, d);
            const Vector3fP wi = d * enoki::sqrt(divp(FloatP(1.f), d2));
            FloatP cosCam(0.f);
            *px = FloatP(0.f);
            *py = FloatP(0.f);
            *We = FloatP(0.f);
            return c.camera->Project(wi, px, py, We, &cosCam, active) &&
                   (d2 > FloatP(EPS_DENOM));
        }

        // Product of the f factors of path `path`  that lie in the window.
        // Edges with index < firstEdge drop their geometric g factor.
        inline SWCSpectrumP EvalCanonWindow(const WalkCtx &c,
                                            const wf::PathStore &ps,
                                            const UInt32P &path, const Int32P &n,
                                            const Int32P &lo, const Int32P &hi,
                                            const Int32P &firstEdge,
                                            const SpectrumWavelengthsP &sw,
                                            bool traceVisibility,
                                            const MaskP &active, MaskP *ok)
        {
            MaskP live = active && (n >= Int32P(2)) && (lo <= hi) &&
                         (lo >= Int32P(0)) && (hi <= n - Int32P(1));
            SWCSpectrumP f(1.f);
            if (none(live))
            {
                *ok = MaskP(false);
                return SWCSpectrumP(0.f);
            }

            const int iters = int(enoki::hmax(select(live, hi - lo, Int32P(0)))) + 1;
            wf::VertexData Vc = LoadChainVertex(c, ps, path, lo, live);

            for (int k = 0; k < iters && any(live); ++k)
            {
                const Int32P vi = lo + Int32P(k);
                const MaskP inWin = live && (vi <= hi);
                if (!any(inWin))
                    break;
                const MaskP hasNext = inWin && (vi < n - Int32P(1));
                const wf::VertexData Vn =
                    LoadChainVertex(c, ps, path, vi + Int32P(1), hasNext);

                const Vector3fP dRaw = Vn.p - Vc.p;
                const FloatP d2 = dot(dRaw, dRaw);
                const Vector3fP wi = dRaw * enoki::sqrt(divp(FloatP(1.f), d2));

                // Camera vertex sensor response We (== camera Pdf).
                const MaskP isCam = inWin && eq(vi, Int32P(0));
                if (any(isCam))
                {
                    FloatP px(0.f), py(0.f), We(0.f), cosCam(0.f);
                    const MaskP inView =
                        c.camera->Project(wi, &px, &py, &We, &cosCam, isCam);
                    f = select(isCam, f * We, f);
                    live = live && (!isCam || (inView && (We > FloatP(0.f))));
                }

                // Scatter vertex folded BSDF toward the next vertex.
                const MaskP isSurf = inWin && live && (vi >= Int32P(1)) &&
                                     (vi <= n - Int32P(2));
                const MaskP isDiff = isSurf && !Vc.spec;
                const MaskP isSpec = isSurf && Vc.spec;
                if (any(isDiff))
                {
                    const VertexTerm vt = EvalVertexTerm(Vc, sw, wi, isDiff);
                    f = select(isDiff, f * vt.f, f);
                    live = live && (!isDiff || vt.ok);
                }
                if (any(isSpec))
                {
                    // Lobe from the side relation where wb points at vi-1, wi at
                    // vi+1. Same side of ng -> reflection, opposite ->
                    // transmission.
                    const FloatP sPrev = dot(Vc.wb, Vc.dg.ng);
                    const FloatP sNext = dot(wi, Vc.dg.ng);
                    const MaskP refl = (sPrev * sNext) > FloatP(0.f);
                    const UInt32P lobe =
                        select(refl,
                               UInt32P(uint32_t(BSDFType::SpecularReflection)),
                               UInt32P(uint32_t(BSDFType::SpecularTransmission)));
                    Vector3fP wiOut;
                    SWCSpectrumP R;
                    Vc.bsdf->SpecularStep(sw, Vc.wb, Vc.dg, lobe,
                                          TransportMode::Radiance, &wiOut, &R,
                                          isSpec);
                    f = select(isSpec, f * R, f);
                    live = live && (!isSpec || (R > SWCSpectrumP(0.f)));
                }

                // Emitter Le toward the previous vertex (wb).
                const MaskP isEmit = inWin && live && eq(vi, n - Int32P(1));
                if (any(isEmit))
                {
                    const LightPtr selLight = enoki::gather<LightPtr>(
                        c.lightPtrs.data(), wf::GatherSelIdx(ps, path, isEmit),
                        isEmit);
                    const SWCSpectrumP Le = selLight->Le(Vc.dg, sw, Vc.wb, isEmit);
                    f = select(isEmit, f * Le, f);
                    live = live && (!isEmit || (Le > SWCSpectrumP(0.f)));
                }

                // Edge i -> i+1 (far cosine at i+1, over d^2, times Tr). Edges
                // below firstEdge drop the geometric g.
                const MaskP edge = hasNext && live && (vi < hi) &&
                                   (d2 > FloatP(EPS_DENOM));
                const MaskP dropG = vi < firstEdge;
                if (any(edge))
                {
                    if (traceVisibility)
                    {
                        const ShadowRng rng{UInt32P(0u), UInt32P(0u), vi,
                                            Int32P(0), UInt32P(0u), 0u};
                        const EdgeTerm et = EvalEdgeTerm(
                            c.shadow, Vc.p, Vc.dg.ng, wi, Vn.p, Vn.dg.ng, Vn.isMed,
                            /*farCos=*/true, sw, wf::StartMedium(Vc, wi), rng, edge);
                        const SWCSpectrumP ef =
                            select(dropG, et.Tr, et.g * et.Tr);
                        f = select(edge, f * ef, f);
                        live = live && (!edge || et.ok);
                    }
                    else
                    {
                        const FloatP g =
                            abs(dot(wi, Vn.dg.ng)) * divp(FloatP(1.f), d2);
                        const FloatP gk = select(dropG, FloatP(1.f), g);
                        f = select(edge, f * gk, f);
                        live = live && (!edge || (g > FloatP(0.f)));
                    }
                }
                else
                {
                    // A lane whose next vertex coincides with this one has a
                    // degenerate edge!
                    live = live && !(hasNext && (vi < hi) &&
                                     !(d2 > FloatP(EPS_DENOM)));
                }

                Vc = Vn;
            }

            *ok = live;
            return select(live, f, SWCSpectrumP(0.f));
        }

        // Whole path value.
        inline SWCSpectrumP EvalCanonF(const WalkCtx &c, const wf::PathStore &ps,
                                       const UInt32P &path, const Int32P &n,
                                       const SpectrumWavelengthsP &sw,
                                       bool traceVisibility, const MaskP &active,
                                       MaskP *ok)
        {
            // Whole path value.
            return EvalCanonWindow(c, ps, path, n, Int32P(0), n - Int32P(1),
                                   Int32P(0), sw, traceVisibility, active, ok);
        }

    } // namespace erpt
} // namespace lux2

#endif // LUX2_INTEGRATORS_ERPT_CANON_PATH_H
