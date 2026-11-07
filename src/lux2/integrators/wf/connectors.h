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

// Wavefront connector stages.

#ifndef LUX2_INTEGRATORS_WF_CONNECTORS_H
#define LUX2_INTEGRATORS_WF_CONNECTORS_H

#include "integrators/core/vertex.h"
#include "integrators/debug/integrator_debug.h"
#include "integrators/lane/direct.h"
#include "integrators/lane/keys.h"
#include "integrators/lane/math.h"
#include "integrators/lane/occlusion.h"
#include "integrators/wf/mis.h"
#include "integrators/wf/pool_view.h"
#include "integrators/wf/sample_table.h"
#include "integrators/wf/storage.h"
#include "integrators/wf/types.h"

#include "core/light.h"
#include "core/math/math.h"

#include <enoki/array.h>

namespace lux2
{
    namespace wf
    {
        // RNG dimension bases.
        inline int LightOriginBaseI(int eyeDepth)
        {
            return 2 + std::max(0, eyeDepth - 1) * 4;
        }
        inline int NeeBaseI(int loBase, int lightDepth)
        {
            return loBase + 6 + std::max(0, lightDepth - 1) * 4;
        }
        inline UInt32P NeeDim(int neeBase, const Int32P &ei)
        {
            return UInt32P(uint32_t(neeBase)) + (UInt32P(ei) - UInt32P(1u)) *
                                                    UInt32P(4u);
        }
        inline int CamNeeBaseI(int neeBase, int eyeDepth)
        {
            return neeBase + 4 * std::max(0, eyeDepth - 1);
        }

        // Shared origin gather for general and camera connectors.
        inline UInt32P GatherSelIdx(const PathStore &ps, const UInt32P &path,
                                    const MaskP &)
        {
            alignas(64) uint32_t pathA[PACKET_WIDTH], idxA[PACKET_WIDTH];
            enoki::store_unaligned(pathA, path);
            for (size_t i = 0; i < PACKET_WIDTH; ++i)
                idxA[i] = ps.lightIdx[pathA[i]];
            return enoki::load_unaligned<UInt32P>(idxA);
        }
        inline FloatP GatherDirectPdf1(const PathStore &ps, const UInt32P &path,
                                       const MaskP &)
        {
            alignas(64) uint32_t pathA[PACKET_WIDTH];
            alignas(64) float pdfA[PACKET_WIDTH];
            enoki::store_unaligned(pathA, path);
            for (size_t i = 0; i < PACKET_WIDTH; ++i)
                pdfA[i] = ps.directPdf1[pathA[i]];
            return enoki::load_unaligned<FloatP>(pdfA);
        }

        // Per-connection identity gathered from the sample table by slot.
        struct ConnIdent
        {
            SpectrumWavelengthsP sw;
            UInt32P seed, sampleIdx;
            FloatP camPdfR0;
        };
        inline ConnIdent LoadIdent(const SampleTable &tbl, const UInt32P &sample)
        {
            const MaskP all = true;
            ConnIdent id;
            id.sw.FromWavelength(
                enoki::gather<FloatP>(tbl.w.data(), sample, all));
            id.seed = enoki::gather<UInt32P>(tbl.seed.data(), sample, all);
            id.sampleIdx = enoki::gather<UInt32P>(tbl.sidx.data(), sample, all);
            id.camPdfR0 = enoki::gather<FloatP>(tbl.camPdfR0.data(), sample, all);
            return id;
        }

        // One connection per live lane at (t,s).
        struct GenConnector
        {
            UInt32P ePath, lPath, sample;
            Int32P nE, nL, t, s;
            MaskP alive;

            const WalkCtx &c;
            SampleTable &tbl;
            const PathStore &eyePs;
            const PathStore &lightPs;
            UInt32P slot; // per-lane light list index for ConnKey RNG

            GenConnector(const WalkCtx &cc, SampleTable &t, const PathStore &ePs,
                         const PathStore &lPs)
                : c(cc), tbl(t), eyePs(ePs), lightPs(lPs)
            {
            }

            // One connection per live lane.
            void Run()
            {
                WfStatGenStep(enoki::count(alive));
                const MaskP base = alive && (nE >= Int32P(2)) && (nL >= Int32P(1));
                if (any(base))
                    Connect(base);
                WfStatGenPacket(1);
            }

        private:
            void Connect(const MaskP &base)
            {
                const ConnIdent cid = LoadIdent(tbl, sample);
                const SpectrumWavelengthsP &sw = cid.sw;
                const UInt32P &seed = cid.seed, &sampleIdx = cid.sampleIdx;
                const Int32P ei = t - Int32P(1);
                const Int32P li = s - Int32P(1);
                const MaskP isOrigin = eq(s, Int32P(1)); // nL==1 branch, per lane

                const VertexData E =
                    LoadVertex(c, eyePs, ePath, ei, base, WF_LOAD_GEN);
                const VertexData L0 =
                    LoadVertex(c, lightPs, lPath, li, base, WF_LOAD_GEN);
                // Origin reads the emitter vertex 0 for one sided
                // emission and direct pdf.
                const VertexData Lz = LoadVertex(c, lightPs, lPath, Int32P(0),
                                                 base && isOrigin, WF_LOAD_GEN);

                const Vector3fP dRaw = L0.p - E.p;
                const FloatP d2 = dot(dRaw, dRaw);
                const Vector3fP ewi = dRaw * enoki::sqrt(divp(FloatP(1.f), d2));
                const Vector3fP lwi = -ewi;

                // Endpoint is phase on scatter lanes, BSDF on surface lanes.
                VertexP evx;
                evx.valid = base;
                evx.isMedium = E.isMed;
                evx.p = E.p;
                evx.dg = E.dg;
                evx.wo = E.wb;
                evx.k = E.k;
                evx.bsdf = E.bsdf;
                evx.med = E.med;
                BSDFEvalP ef = ZeroEval();
                FloatP ecosiV(0.f);
                EvalVertex(evx, sw, ewi, &ef, &ecosiV, base);

                // Light endpoint origin uses the analytic emitter, deeper vertices
                // use the BSDF/phase. TODO: both branches evaluated, masked per lane.
                const FloatP front = dot(lwi, Lz.dg.ng);
                const FloatP oLfv =
                    select(front > FloatP(0.f), abs(dot(lwi, Lz.dg.n)), FloatP(0.f));
                const FloatP oLpdf = oLfv * INVPI;

                BSDFEvalP lf = ZeroEval();
                L0.bsdf->Eval(sw, L0.wb, lwi, L0.dg, TransportMode::Importance, &lf,
                              base && !isOrigin && !L0.isMed);
                const FloatP ph = select(
                    L0.isMed,
                    L0.med->PhasePdf(sw, L0.p, L0.wb, lwi, base && !isOrigin && L0.isMed),
                    FloatP(0.f));
                const FloatP dLfv = select(L0.isMed, ph, lf.f);
                const FloatP dLpdf = select(L0.isMed, ph, lf.pdfRev);
                const FloatP dLpdfR = select(L0.isMed, ph, lf.pdf);

                const FloatP lfv = select(isOrigin, oLfv, dLfv);
                const FloatP lpdf = select(isOrigin, oLpdf, dLpdf);
                const FloatP lpdfR = select(isOrigin, FloatP(0.f), dLpdfR);

                const SWCSpectrumP Lc0 = L0.flux * lfv * ef.f * E.flux;
                const MaskP preCont = base && (d2 > FloatP(EPS_DENOM)) &&
                                      (Lc0 > SWCSpectrumP(0.f));
                if (!any(preCont))
                    return;

                // Zero ng = no offset.
                const Point3fP shadowO = OffsetRay(E.p, E.dg.ng, ewi, FloatP(EPS_RAY));
                const FloatP dist = enoki::sqrt(d2);
                const FloatP tmax =
                    dist - enoki::max(FloatP(1e-3f), FloatP(1e-3f) * dist);
                const UInt32P startMed = StartMedium(E, ewi);
                const ShadowRng rng{seed, sampleIdx, t, Int32P(0),
                                    ConnKey(slot, t, s), BDPT_CONN_SALT};
                const Occlusion oc =
                    Occlude(c.shadow, shadowO, ewi, startMed, tmax, sw, rng, preCont);
                const SWCSpectrumP Tr = oc.Tr;
                const MaskP cont = oc.ok;
                if (!any(cont))
                    return;
                const SWCSpectrumP Lc = Lc0 * divp(FloatP(1.f), d2) * Tr;

                const FloatP ecosi =
                    select(E.isMed, FloatP(1.f), abs(dot(ewi, E.dg.ng)));
                const FloatP lcoso =
                    select(L0.isMed, FloatP(1.f), abs(dot(lwi, L0.dg.ng)));

                // Eye endpoint transient words.
                Transient tr;
                tr.isLightDirect = false;
                const FloatP invD2 = divp(FloatP(1.f), d2);
                tr.eAF_last = E.dARW;
                tr.eAR_last = lpdf * ecosi * invD2;
                // eye[nE-2].dAWeight = ef.pdfRev * cosi[ei-1] / d2[ei-1]
                const MisPair Ep = LoadMisPair(eyePs, ePath, ei - Int32P(1), base);
                tr.eAR_prev = ef.pdfRev * Ep.cosi * divp(FloatP(1.f), Ep.d2);
                {
                    const FloatP den = ecosi * ef.pdfRev;
                    tr.eRR_last = select(den > FloatP(EPS_DENOM),
                                         min(FloatP(1.f),
                                             max(c.lightRR,
                                                 ef.f * E.coso * divp(FloatP(1.f), den))),
                                         FloatP(0.f));
                }
                tr.eRRR_last =
                    select(ef.pdf > FloatP(EPS_DENOM),
                           min(FloatP(1.f),
                               max(c.eyeRR, ef.f * divp(FloatP(1.f), ef.pdf))),
                           FloatP(0.f));

                // Light endpoint transient words.
                tr.lAR_last = ef.pdf * lcoso * invD2;
                const MisPair Lp =
                    LoadMisPair(lightPs, lPath, li - Int32P(1), base && !isOrigin);
                tr.lAR_prev = select(isOrigin, FloatP(0.f),
                                     lpdfR * Lp.coso * divp(FloatP(1.f), Lp.d2));
                tr.lRR_last =
                    select(lpdf > FloatP(EPS_DENOM),
                           min(FloatP(1.f),
                               max(c.lightRR, lfv * divp(FloatP(1.f), lpdf))),
                           FloatP(0.f));
                {
                    const FloatP den = lcoso * lpdfR;
                    const FloatP dRRR =
                        select(den > FloatP(EPS_DENOM),
                               min(FloatP(1.f),
                                   max(c.eyeRR,
                                       lfv * L0.cosi * divp(FloatP(1.f), den))),
                               FloatP(0.f));
                    tr.lRRR_last = select(isOrigin, FloatP(1.f), dRRR);
                }

                // Origin -> direct sampling of the emitter from the eye vertex.
                const UInt32P selIdx = GatherSelIdx(lightPs, lPath, cont);
                const LightPtr selLight =
                    enoki::gather<LightPtr>(c.lightPtrs.data(), selIdx, cont);
                const FloatP pD =
                    enoki::gather<FloatP>(c.lsD->pSel.data(), selIdx, cont);
                const FloatP pdfSolid =
                    selLight->Pdf_L(E.p, E.dg.n, Lz.p, Lz.dg.n, cont && isOrigin,
                                    E.recvHasN) *
                    pD;
                const FloatP oPdfLD =
                    ConvertDensity(E.p, pdfSolid, Lz.p, Lz.dg.ng);
                const FloatP dPdfLD = GatherDirectPdf1(lightPs, lPath, cont);
                tr.pdfLightDirect = select(isOrigin, oPdfLD, dPdfLD);

                Breakdown bd;
                const PoolView eyeV{&eyePs, ePath};
                const PoolView lightV{&lightPs, lPath};
                const FloatP w = divp(
                    FloatP(1.f),
                    WeightPathV(c, eyeV, t, lightV, s, tr, cont));
                AddToSample(tbl.L.data(), sample, Lc * w, cont);
            }
        };

        // NEE at an eye vertex.
        struct NeeConnector
        {
            UInt32P ePath, sample;
            Int32P nE, t;
            MaskP alive;

            const WalkCtx &c;
            const Sampler &sampler;
            SampleTable &tbl;
            const PathStore &eyePs;

            NeeConnector(const WalkCtx &cc, const Sampler &sa, SampleTable &t,
                         const PathStore &ePs)
                : c(cc), sampler(sa), tbl(t), eyePs(ePs)
            {
            }

            // One NEE connection per live lane.
            void Run()
            {
                const MaskP base = alive && (nE >= Int32P(2)) && (c.nLights > 0);
                if (any(base))
                    Connect(base);
            }

        private:
            void Connect(const MaskP &base)
            {
                const ConnIdent cid = LoadIdent(tbl, sample);
                const SpectrumWavelengthsP &sw = cid.sw;
                const UInt32P &seed = cid.seed, &sampleIdx = cid.sampleIdx;
                const Int32P ei = t - Int32P(1);
                const VertexData E =
                    LoadVertex(c, eyePs, ePath, ei, base, WF_LOAD_NEE);

                VertexP evx;
                evx.valid = base;
                evx.isMedium = E.isMed;
                evx.p = E.p;
                evx.dg = E.dg;
                evx.wo = E.wb;
                evx.k = E.k;
                evx.bsdf = E.bsdf;
                evx.med = E.med;

                const int neeBase = NeeBaseI(LightOriginBaseI(c.eyeDepth),
                                             c.lightDepth);
                const UInt32P nd = NeeDim(neeBase, ei);
                ForEachNeeLight(
                    c, SamplerSource{sampler, seed, sampleIdx}, base,
                    [&](uint32_t s)
                    {
                        return NEE_ALL_DIM +
                               (UInt32P(ei) * UInt32P(c.lsD->TaskCount()) +
                                UInt32P(s)) *
                                   UInt32P(2u);
                    },
                    NeeOneDims{nd, nd + UInt32P(1u), nd + UInt32P(3u)},
                    [&](const auto &light, const UInt32P &lightIdx,
                        const UInt32P &shadowKey, const FloatP &infF,
                        const FloatP &pSel, const Point2fP &uPos,
                        const FloatP &uComp, const MaskP &)
                    {
                        DirectOne(base, E, evx, ei, light, lightIdx, shadowKey,
                                  infF, pSel, uPos, uComp);
                    });
            }

            void DirectOne(const MaskP &base,
                           const VertexData &E, const VertexP &evx,
                           const Int32P &ei, const auto &light,
                           const UInt32P &lightIdx, const UInt32P &shadowKey,
                           const FloatP &infF, const FloatP &pSel,
                           const Point2fP &uPos, const FloatP &uComp)
            {
                const ConnIdent cid = LoadIdent(tbl, sample);
                const SpectrumWavelengthsP &sw = cid.sw;
                const UInt32P &seed = cid.seed, &sampleIdx = cid.sampleIdx;
                const LiSample li = SampleLi(sw, light, infF, E.p, E.dg.n,
                                             E.recvHasN, pSel, uPos, uComp, base);
                const FloatP d2 = li.dist * li.dist;

                const ShadowRng rng{seed, sampleIdx, t, Int32P(0), shadowKey,
                                    SHADOW_SALT};
                const DirectEval de = ShadeDirect(
                    c.shadow, evx, sw, li, rng,
                    [&](const Vector3fP &wiL)
                    { return StartMedium(E, wiL); });
                const MaskP cont = de.ok;
                if (!any(cont))
                    return;

                const BSDFEvalP ef = de.ev;
                const FloatP ecosi =
                    select(E.isMed, FloatP(1.f), abs(dot(li.wi, E.dg.ng)));
                const SWCSpectrumP Lc =
                    E.flux * ef.f * li.Le * de.Tr * divp(FloatP(1.f), li.pdfL);

                Transient tr;
                const FloatP invD2 = divp(FloatP(1.f), d2);
                const FloatP cosLraw = dot(-li.wi, li.n);
                const FloatP cosL =
                    select(cosLraw > FloatP(0.f), cosLraw, FloatP(0.f));
                const FloatP lpdf = cosL * INVPI;
                const FloatP area =
                    enoki::gather<FloatP>(c.areaByIdx.data(), lightIdx, cont);
                const FloatP pLP =
                    enoki::gather<FloatP>(c.lsP->pSel.data(), lightIdx, cont);
                tr.isLightDirect = true;
                tr.pdfLightDirect = ConvertDensity(E.p, li.pdfL, li.p, li.n);
                tr.lAR_last = ef.pdf * cosL * invD2;
                tr.lRR_last =
                    select(lpdf > FloatP(EPS_DENOM),
                           min(FloatP(1.f),
                               max(c.lightRR, cosL * divp(FloatP(1.f), lpdf))),
                           FloatP(0.f));
                tr.lRRR_last = FloatP(1.f);

                tr.eAF_last = E.dARW;
                tr.eAR_last = lpdf * ecosi * invD2;
                const MisPair Ep = LoadMisPair(eyePs, ePath, ei - Int32P(1), base);
                tr.eAR_prev = ef.pdfRev * Ep.cosi * divp(FloatP(1.f), Ep.d2);
                {
                    const FloatP den = ecosi * ef.pdfRev;
                    tr.eRR_last = select(den > FloatP(EPS_DENOM),
                                         min(FloatP(1.f),
                                             max(c.lightRR,
                                                 ef.f * E.coso *
                                                     divp(FloatP(1.f), den))),
                                         FloatP(0.f));
                }
                tr.eRRR_last =
                    select(ef.pdf > FloatP(EPS_DENOM),
                           min(FloatP(1.f),
                               max(c.eyeRR, ef.f * divp(FloatP(1.f), ef.pdf))),
                           FloatP(0.f));

                Breakdown bd;
                const PoolView eyeV{&eyePs, ePath};
                const Virtual1 lightV{pLP * divp(FloatP(1.f), area)};
                const FloatP w =
                    divp(FloatP(1.f),
                         WeightPathV(c, eyeV, t, lightV, Int32P(1), tr, cont));
                AddToSample(tbl.L.data(), sample, Lc * w, cont);
            }
        };

        // Camera vertex NEE.
        struct CamAbaSink : Film
        {
            FloatP acc{0.f};
            int XRes() const override { return 1; }
            int YRes() const override { return 1; }
            void Splat(const FloatP &, const FloatP &, const SWCSpectrumP &,
                       const SpectrumWavelengthsP &, const FloatP &,
                       const FloatP &) override
            {
            }
            void SplatLight(const FloatP &, const FloatP &, const SWCSpectrumP &L,
                            const SpectrumWavelengthsP &, MaskP active) override
            {
                acc = select(active, acc + L, acc);
            }
            void Merge(Film *) override {}
            void AddSampleCount(double) override {}
            double SampleCount() const override { return 0.0; }
            bool WriteImage(ImageType) override { return false; }
        };

        struct CamNeeConnector
        {
            UInt32P ePath, sample;
            MaskP alive;

            const WalkCtx &c;
            const Sampler &sampler;
            SampleTable &tbl;
            const PathStore &eyePs;
            Film *film;

            CamNeeConnector(const WalkCtx &cc, const Sampler &sa, SampleTable &t,
                            const PathStore &ePs, Film *film_)
                : c(cc), sampler(sa), tbl(t), eyePs(ePs), film(film_)
            {
            }

            void Run()
            {
                const MaskP base = alive && (c.nLights > 0);
                if (!any(base))
                    return;
                const ConnIdent cid = LoadIdent(tbl, sample);
                const SpectrumWavelengthsP &sw = cid.sw;
                const UInt32P &seed = cid.seed, &sampleIdx = cid.sampleIdx;
                const FloatP &camPdfR0 = cid.camPdfR0;
                const VertexData E0 =
                    LoadVertex(c, eyePs, ePath, Int32P(0), base, WF_LOAD_CAMNEE);

                const int camNeeBase = CamNeeBaseI(
                    NeeBaseI(LightOriginBaseI(c.eyeDepth), c.lightDepth),
                    c.eyeDepth);
                const UInt32P nd = UInt32P(uint32_t(camNeeBase));
                ForEachNeeLight(
                    c, SamplerSource{sampler, seed, sampleIdx}, base,
                    [&](uint32_t s)
                    { return NEE_ALL_DIM + UInt32P(s) * UInt32P(2u); },
                    NeeOneDims{nd, nd + UInt32P(1u), nd + UInt32P(3u)},
                    [&](const auto &light, const UInt32P &lightIdx,
                        const UInt32P &shadowKey, const FloatP &infF,
                        const FloatP &pSel, const Point2fP &uPos,
                        const FloatP &uComp, const MaskP &)
                    {
                        DirectOne(base, E0, light, lightIdx, shadowKey, infF, pSel,
                                  uPos, uComp);
                    });
            }

        private:
            void DirectOne(const MaskP &base, const VertexData &E0,
                           const auto &light, const UInt32P &lightIdx,
                           const UInt32P &shadowKey, const FloatP &infF,
                           const FloatP &pSel, const Point2fP &uPos,
                           const FloatP &uComp)
            {
                const ConnIdent cid = LoadIdent(tbl, sample);
                const SpectrumWavelengthsP &sw = cid.sw;
                const UInt32P &seed = cid.seed, &sampleIdx = cid.sampleIdx;
                const FloatP &camPdfR0 = cid.camPdfR0;
                // A camera vertex has no shading normal to test.
                const LiSample li =
                    SampleLi(sw, light, infF, E0.p, Normal3fP(0.f), MaskP(false),
                             pSel, uPos, uComp, base);
                const FloatP d2 = li.dist * li.dist;
                const Vector3fP &ewi = li.wi;

                FloatP px(0.f), py(0.f), We(0.f), cosCam(0.f);
                const MaskP inView =
                    c.camera->Project(ewi, &px, &py, &We, &cosCam, li.ok);
                const MaskP tryVis =
                    inView && (li.pdfL > FloatP(0.f)) && (We > FloatP(0.f));
                if (none(tryVis))
                    return;

                const ShadowRng rng{seed, sampleIdx, Int32P(1), Int32P(0),
                                    shadowKey, SHADOW_SALT};
                const Occlusion oc = Occlude(c.shadow, E0.p, ewi, c.cameraMediumID,
                                             li.tmax, sw, rng, tryVis);
                const SWCSpectrumP Tr = oc.Tr;
                const MaskP cont = oc.ok;
                if (!any(cont))
                    return;

                const SWCSpectrumP Lc =
                    We * li.Le * Tr * divp(FloatP(1.f), li.pdfL);

                Transient tr;
                const FloatP invD2 = divp(FloatP(1.f), d2);
                const FloatP cosLraw = dot(-li.wi, li.n);
                const FloatP cosL =
                    select(cosLraw > FloatP(0.f), cosLraw, FloatP(0.f));
                const FloatP lpdf = cosL * INVPI;
                const FloatP area =
                    enoki::gather<FloatP>(c.areaByIdx.data(), lightIdx, cont);
                const FloatP pLP =
                    enoki::gather<FloatP>(c.lsP->pSel.data(), lightIdx, cont);
                tr.isLightDirect = true;
                tr.pdfLightDirect = ConvertDensity(E0.p, li.pdfL, li.p, li.n);
                tr.lAR_last = We * cosL * invD2;
                tr.lRR_last =
                    select(lpdf > FloatP(EPS_DENOM),
                           min(FloatP(1.f),
                               max(c.lightRR, cosL * divp(FloatP(1.f), lpdf))),
                           FloatP(0.f));
                tr.lRRR_last = FloatP(1.f);

                // The camera endpoint.
                tr.eAF_last = camPdfR0 * cosCam * invD2;
                tr.eAR_last = lpdf * cosCam * invD2;
                tr.eAR_prev = FloatP(0.f);
                tr.eRR_last = FloatP(1.f);
                tr.eRRR_last = FloatP(1.f);

                Breakdown bd;
                const NullView eyeV;
                const Virtual1 lightV{pLP * divp(FloatP(1.f), area)};
                const FloatP w =
                    divp(FloatP(1.f),
                         WeightPathV(c, eyeV, Int32P(1), lightV, Int32P(1), tr,
                                     cont));
                film->SplatLight(px, py, Lc * w, sw, cont);
            }
        };

        // Connect a light subpath vertex to the camera.
        struct CamConnector
        {
            UInt32P ePath, lPath, sample;
            Int32P nL, s;
            MaskP alive;

            const WalkCtx &c;
            SampleTable &tbl;
            const PathStore &eyePs;
            const PathStore &lightPs;
            UInt32P slot;
            Film *film;

            CamConnector(const WalkCtx &cc, SampleTable &t, const PathStore &ePs,
                         const PathStore &lPs, Film *film_)
                : c(cc), tbl(t), eyePs(ePs), lightPs(lPs), film(film_)
            {
            }

            // One camera connection per live lane.
            void Run()
            {
                const MaskP base = alive && (nL >= Int32P(1));
                if (any(base))
                    Connect(base);
            }

        private:
            void Connect(const MaskP &base)
            {
                const ConnIdent cid = LoadIdent(tbl, sample);
                const SpectrumWavelengthsP &sw = cid.sw;
                const UInt32P &seed = cid.seed, &sampleIdx = cid.sampleIdx;
                const FloatP &camPdfR0 = cid.camPdfR0;
                const Int32P li = s - Int32P(1);
                const MaskP isOrigin = eq(s, Int32P(1));
                const VertexData E0 =
                    LoadVertex(c, eyePs, ePath, Int32P(0), base, WF_LOAD_CAM);
                const VertexData L0 =
                    LoadVertex(c, lightPs, lPath, li, base, WF_LOAD_CAM);
                const VertexData Lz = LoadVertex(c, lightPs, lPath, Int32P(0),
                                                 base && isOrigin, WF_LOAD_CAM);

                const Vector3fP dRaw = L0.p - E0.p;
                const FloatP d2 = dot(dRaw, dRaw);
                const Vector3fP ewi = dRaw * enoki::sqrt(divp(FloatP(1.f), d2));
                const Vector3fP lwi = -ewi;

                FloatP px(0.f), py(0.f), We(0.f), cosCam(0.f);
                const MaskP inView =
                    c.camera->Project(ewi, &px, &py, &We, &cosCam, base);
                if (!any(inView))
                    return;

                // Light endpoint.
                const FloatP front = dot(lwi, Lz.dg.ng);
                const FloatP oLfv =
                    select(front > FloatP(0.f), abs(dot(lwi, Lz.dg.n)), FloatP(0.f));
                const FloatP oLpdf = oLfv * INVPI;

                BSDFEvalP lf = ZeroEval();
                L0.bsdf->Eval(sw, L0.wb, lwi, L0.dg, TransportMode::Importance, &lf,
                              inView && !isOrigin && !L0.isMed);
                const FloatP ph = select(
                    L0.isMed,
                    L0.med->PhasePdf(sw, L0.p, L0.wb, lwi,
                                     inView && !isOrigin && L0.isMed),
                    FloatP(0.f));
                const FloatP dLfv = select(L0.isMed, ph, lf.f);
                const FloatP dLpdf = select(L0.isMed, ph, lf.pdfRev);
                const FloatP dLpdfR = select(L0.isMed, ph, lf.pdf);

                const FloatP lfv = select(isOrigin, oLfv, dLfv);
                const FloatP lpdf = select(isOrigin, oLpdf, dLpdf);
                const FloatP lpdfR = select(isOrigin, FloatP(0.f), dLpdfR);

                // Camera BSDF F = We (e.g. PerspectiveBSDF::F).
                const SWCSpectrumP Lc0 = L0.flux * lfv * We;
                const MaskP preCont = inView && (d2 > FloatP(EPS_DENOM)) &&
                                      (Lc0 > SWCSpectrumP(0.f));
                if (!any(preCont))
                    return;

                const FloatP dist = enoki::sqrt(d2);
                const FloatP tmax =
                    dist - enoki::max(FloatP(1e-3f), FloatP(1e-3f) * dist);
                const ShadowRng rng{seed, sampleIdx, Int32P(0), Int32P(0),
                                    ConnKey(slot, Int32P(1), s),
                                    BDPT_CONN_SALT};
                const Occlusion oc = Occlude(c.shadow, E0.p, ewi, c.cameraMediumID,
                                             tmax, sw, rng, preCont);
                const SWCSpectrumP Tr = oc.Tr;
                const MaskP cont = oc.ok;
                if (!any(cont))
                    return;
                const SWCSpectrumP Lc = Lc0 * divp(FloatP(1.f), d2) * Tr;

                const FloatP lcoso =
                    select(L0.isMed, FloatP(1.f), abs(dot(lwi, L0.dg.ng)));
                const FloatP invD2 = divp(FloatP(1.f), d2);

                Transient tr;
                tr.eAF_last = camPdfR0 * cosCam * invD2;
                tr.eAR_last = lpdf * cosCam * invD2;
                tr.eAR_prev = FloatP(0.f);
                tr.eRR_last = FloatP(1.f);
                tr.eRRR_last = FloatP(1.f);
                tr.lAR_last = We * lcoso * invD2;
                const MisPair Lp =
                    LoadMisPair(lightPs, lPath, li - Int32P(1), base && !isOrigin);
                tr.lAR_prev = select(isOrigin, FloatP(0.f),
                                     lpdfR * Lp.coso * divp(FloatP(1.f), Lp.d2));
                tr.lRR_last =
                    select(lpdf > FloatP(EPS_DENOM),
                           min(FloatP(1.f),
                               max(c.lightRR, lfv * divp(FloatP(1.f), lpdf))),
                           FloatP(0.f));
                {
                    const FloatP den = lcoso * lpdfR;
                    const FloatP dRRR =
                        select(den > FloatP(EPS_DENOM),
                               min(FloatP(1.f),
                                   max(c.eyeRR,
                                       lfv * L0.cosi * divp(FloatP(1.f), den))),
                               FloatP(0.f));
                    tr.lRRR_last = select(isOrigin, FloatP(1.f), dRRR);
                }
                // Origin -> direct sampling of the emitter from the camera.
                const UInt32P selIdx = GatherSelIdx(lightPs, lPath, cont);
                const LightPtr selLight =
                    enoki::gather<LightPtr>(c.lightPtrs.data(), selIdx, cont);
                const FloatP pD =
                    enoki::gather<FloatP>(c.lsD->pSel.data(), selIdx, cont);
                const FloatP pdfSolid =
                    selLight->Pdf_L(E0.p, Normal3fP(0.f), Lz.p, Lz.dg.n,
                                    cont && isOrigin, MaskP(false)) *
                    pD;
                const FloatP oPdfLD =
                    ConvertDensity(E0.p, pdfSolid, Lz.p, Lz.dg.ng);
                const FloatP dPdfLD = GatherDirectPdf1(lightPs, lPath, cont);
                tr.pdfLightDirect = select(isOrigin, oPdfLD, dPdfLD);
                tr.isLightDirect = false;

                Breakdown bd;
                const NullView eyeV;
                const PoolView lightV{&lightPs, lPath};
                const FloatP w =
                    divp(FloatP(1.f),
                         WeightPathV(c, eyeV, Int32P(1), lightV, s, tr, cont));
                film->SplatLight(px, py, Lc * w, sw, cont);
            }
        };

    } // namespace wf
} // namespace lux2

#endif // LUX2_INTEGRATORS_WF_CONNECTORS_H
