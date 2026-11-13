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

// Bidirectional's consumers of the shared walker.

#ifndef LUX2_INTEGRATORS_CONSUMERS_BIDIRECTIONAL_CONSUMER_H
#define LUX2_INTEGRATORS_CONSUMERS_BIDIRECTIONAL_CONSUMER_H

#include "integrators/core/vertex.h"
#include "integrators/lane/advance.h"
#include "integrators/lane/geom.h"
#include "integrators/lane/jobs.h"
#include "integrators/lane/lane_core.h"
#include "integrators/lane/math.h"
#include "integrators/lane/sampler_source.h"
#include "integrators/wf/lanes.h"
#include "integrators/wf/mis.h"
#include "integrators/wf/sample_table.h"
#include "integrators/wf/storage.h"
#include "integrators/wf/types.h"

#include "core/camera.h"
#include "core/light.h"
#include "core/light_call.h"
#include "core/math/math.h"
#include "core/sampler.h"

#include <enoki/array.h>

namespace lux2
{
    namespace wf
    {
        // RNG dimension layout.
        inline UInt32P EyeDim(int v) { return UInt32P(uint32_t(2 + (v - 1) * 4)); }

        // Build a geom record from an Advance event.
        inline VertexGeomP BuildGeom(const Event &ev, const MaskP &validVtx,
                                     const SWCSpectrumP &flux)
        {
            const HitP &h = ev.hit;
            VertexGeomP g;
            g.p = select(validVtx, h.p, Point3fP(0.f));
            // ev.dg is only resolved for surface events.
            const MaskP surf = validVtx && ev.surface;
            g.n = select(surf, ev.dg.n, Normal3fP(0.f));
            g.ng = select(surf, ev.dg.ng, Normal3fP(0.f));
            g.wb = Vector3fP(0.f); // filled by caller (wo)
            g.dp_du = h.dp_du;
            g.dp_dv = h.dp_dv;
            g.dp_ds = h.dp_ds;
            g.dp_dt = h.dp_dt;
            g.uv = h.uv;
            g.flux = flux;
            g.k = ev.k;
            g.matID = select(ev.surface, h.matID, UInt32P(0u));
            // Scatter lanes sit in the ray medium; surface lanes store both
            // sides from the hit.
            g.medID = UInt32P(LUX2_MEDIUM_CLEAR);
            g.medFront = UInt32P(LUX2_MEDIUM_CLEAR);
            g.medBack = UInt32P(LUX2_MEDIUM_CLEAR);
            return g;
        }

        // Eye store walker.
        class BidirEyeConsumer
        {
        public:
            using Lanes = EyeLanes;

            BidirEyeConsumer(const WalkCtx &c, PathStore *ps, SampleTable *tbl)
                : m_c(c), m_ps(ps), m_tbl(tbl)
            {
                // Start every lane dead.
                mL = EyeLanes::Fresh(PixelPlan{});
                mL.core.alive = MaskP(false);
            }

            // RNG stream accessors. The bounce stream is the vertex count.
            Int32P Bounce(const LaneCore &) const { return mL.v; }
            Int32P DepthBase(const LaneCore &) const { return Int32P(0); }

            // An emissive surface (or Null) hit at the current
            // throughput.
            void OnPassThrough(LaneCore &core, const HitP &h, const MaskP &m)
            {
                MaskP emit = m && (h.lightID >= Int32P(0));
                if (!any(emit))
                    return;
                if (m_c.multiGroup)
                {
                    const Int32P g = enoki::gather<Int32P>(
                        m_c.areaGroup.data(), h.lightID, emit);
                    emit = emit && eq(g, Int32P(m_c.activeGroup));
                }
                if (!any(emit))
                    return;

                const DifferentialGeometryP dg = MakeDG(h, core.d, emit);
                const Vector3fP wo = -core.d;
                const LightPtr al = enoki::gather<LightPtr>(m_c.areaPtrs.data(),
                                                            h.lightID, emit);
                const SWCSpectrumP Le = al->Le(dg, core.sw, wo, emit);
                const SWCSpectrumP Lc = core.beta * Le;
                const MaskP cont = emit && (Lc > SWCSpectrumP(0.f));
                if (!any(cont))
                    return;

                // Connection vertex is index v; eye has vertices 0..v-1.
                const Int32P nE = mL.v + Int32P(1);
                const Vector3fP dv = h.p - mL.prevP;
                const FloatP d2 = dot(dv, dv);
                const FloatP invD2 = divp(FloatP(1.f), d2);
                const Vector3fP wDir = dv * enoki::sqrt(invD2);
                const FloatP cB = abs(dot(wDir, h.ngeo)); // v.coso

                const FloatP area =
                    enoki::gather<FloatP>(m_c.areaById.data(), h.lightID, cont);
                const FloatP emitPdf = abs(dot(wo, dg.n)) * INVPI; // v.pdf

                Transient tr;
                // prevPdfR is camera-seeded at refill, so v==1 carries the
                // camera arrival density.
                tr.eAF_last = mL.prevPdfR * cB * invD2;
                // pdfLightDirect uses lsD (the direct strategy's selection);
                // the light-path term uses lsP.
                const FloatP pLP = enoki::gather<FloatP>(m_c.lsP->areaPSel.data(),
                                                         h.lightID, cont);
                tr.eAR_last = pLP * divp(FloatP(1.f), area);
                tr.eAR_prev = emitPdf * mL.prevCosi * invD2;
                tr.eRR_last = FloatP(1.f);
                tr.eRRR_last = FloatP(1.f);
                const FloatP pD = enoki::gather<FloatP>(m_c.lsD->areaPSel.data(),
                                                        h.lightID, cont);
                const FloatP pdfSolid =
                    al->Pdf_L(mL.prevP, mL.prevN, h.p, dg.n, cont, mL.prevRecvHasN) *
                    pD;
                tr.pdfLightDirect = ConvertDensity(mL.prevP, pdfSolid, h.p, h.ngeo);
                tr.isLightDirect = false;

                const PoolView eye{m_ps, mL.path};
                const NullView light{};
                const FloatP w =
                    divp(FloatP(1.f),
                         WeightPathV(m_c, eye, nE, light, Int32P(0), tr, cont));
                AddToSample(m_tbl->L.data(), mL.slot, Lc * w, cont);
            }

            // Environment escape.
            void OnEvent(LaneCore &core, const Event &ev)
            {
                m_mediumEvent = ev.scatter;
                const MaskP miss = core.alive && ev.escape;
                mL.alpha = select(miss && eq(mL.v, Int32P(1)), FloatP(0.f), mL.alpha);
                core.alive &= !miss;
            }

            // Write vertex v, sample the continuation, patch the previous vertex's
            // area weight.
            void OnVertex(const SamplerSource &src, LaneCore &core, const Event &ev)
            {
                MaskP &alive = core.alive;
                const MaskP mediumEvent = m_mediumEvent;
                const MaskP hitMask = ev.surface;
                const Vector3fP wo = -core.d;
                const MaskP validVtx = (hitMask | mediumEvent) && alive;
                if (!any(validVtx))
                    return;

                // Depth limit!
                alive &= !(validVtx && mL.v >= Int32P(m_c.eyeDepth));
                const MaskP writeV = validVtx && alive;
                if (!any(writeV))
                    return;

                // The first surface hit sets alpha.
                mL.alpha = select(hitMask && eq(mL.v, Int32P(1)), FloatP(1.f), mL.alpha);

                const Point3fP pNew = ev.hit.p;
                const Vector3fP dv = pNew - mL.prevP;
                const FloatP d2 = dot(dv, dv);
                const FloatP cB = select(mediumEvent, FloatP(1.f),
                                         abs(dot(wo, ev.dg.ng)));

                // Write vertex into the pool.
                VertexGeomP g = BuildGeom(ev, writeV, core.beta);
                g.wb = wo;
                g.medID = select(mediumEvent, core.medium, UInt32P(LUX2_MEDIUM_CLEAR));
                g.medFront =
                    select(mediumEvent, core.medium, ev.hit.mediumFrontID);
                g.medBack = select(mediumEvent, core.medium, ev.hit.mediumBackID);
                const UInt32P rec = RecIdx(*m_ps, mL.path, mL.v, writeV);
                StoreGeom(m_ps->arena->geomWords.data(), g, rec, writeV);

                // dARW[v] = pdfR[v-1] * cB / d2. prevPdfR is camera seeded at
                // refill, so v==1 stores the camera-arrival density.
                VertexMisP mv;
                mv.dAW = FloatP(0.f); // patched at step v+1
                mv.dARW = select(writeV,
                                 mL.prevPdfR * cB * divp(FloatP(1.f), d2), FloatP(0.f));
                mv.cosi = FloatP(0.f); // set after sampling
                mv.coso = cB;
                mv.d2 = d2;
                mv.rr = FloatP(1.f);
                mv.rrR = FloatP(1.f);
                mv.bits = UInt32P(0u);
                StoreMis(m_ps->arena->misWords.data(), mv, rec, writeV);

                // Patch previous vertex-1 d2 now; dAW after sampling.
                const MaskP hasPrevIdx = writeV && (mL.v >= Int32P(1));
                const UInt32P recPrev =
                    RecIdx(*m_ps, mL.path, mL.v - Int32P(1), hasPrevIdx);
                StoreWord(m_ps->arena->misWords.data(),
                          reinterpret_array<UInt32P>(d2),
                          RecWord<MIS_WORDS>(recPrev, kWord_d2), hasPrevIdx);

                // Lanes that continue, only when v+1 < depth.
                const MaskP sampleV = writeV && ((mL.v + Int32P(1)) < Int32P(m_c.eyeDepth));

                VertexP vtx;
                vtx.valid = sampleV;
                vtx.isMedium = mediumEvent;
                vtx.p = pNew;
                vtx.dg = ev.dg;
                vtx.wo = wo;
                vtx.k = ev.k;
                vtx.bsdf = ev.bsdf;
                vtx.med = ev.med;

                // dim = 2 + (v - 1) * 4.
                const UInt32P dim =
                    UInt32P(2u) + (UInt32P(mL.v) - UInt32P(1u)) * UInt32P(4u);
                const Point2fP u2bsdf = src.Get2D(dim);
                const FloatP ucomp = src.Get1D(dim + UInt32P(2u));
                m_urr = src.Get1D(dim + UInt32P(3u));

                SampleVertex(vtx, core.sw, u2bsdf.x(), u2bsdf.y(), ucomp, &m_sample,
                             sampleV);
                const MaskP specular =
                    hitMask && has_flag(m_sample.sampledType, BSDFType::Specular);
                const MaskP sampOk = sampleV && (m_sample.pdf > FloatP(0.f));

                BSDFEvalP ev2 = ZeroEval();
                FloatP vtxCos(0.f);
                EvalVertex(vtx, core.sw, m_sample.wi, &ev2, &vtxCos,
                           sampleV && !specular);

                const FloatP pdfR = m_sample.pdf;
                const FloatP pdf = select(specular, m_sample.pdf, ev2.pdfRev);
                const FloatP cN = select(mediumEvent, FloatP(1.f),
                                         abs(dot(m_sample.wi, ev.dg.ng)));

                // Patch prev vertex dAW[v-1] = pdf[v] * cosi[v-1] / d2 (sampOk).
                StoreWord(m_ps->arena->misWords.data(),
                          reinterpret_array<UInt32P>(
                              select(sampOk, pdf * mL.prevCosi * divp(FloatP(1.f), d2),
                                     FloatP(0.f))),
                          RecWord<MIS_WORDS>(recPrev, kWord_dAW),
                          hasPrevIdx && (mL.v >= Int32P(2)));

                // rr/rrR/cosi/spec for vertex v; write into the MIS record stored.
                const FloatP f = m_sample.f;
                const FloatP rr =
                    min(FloatP(1.f), max(m_c.lightRR, f * cB * divp(FloatP(1.f), cN)));
                const FloatP rrR = min(FloatP(1.f), max(m_c.eyeRR, f));
                StoreWord(m_ps->arena->misWords.data(),
                          reinterpret_array<UInt32P>(rr),
                          RecWord<MIS_WORDS>(rec, kWord_rr), sampleV);
                StoreWord(m_ps->arena->misWords.data(),
                          reinterpret_array<UInt32P>(rrR),
                          RecWord<MIS_WORDS>(rec, kWord_rrR), sampleV);
                StoreWord(m_ps->arena->misWords.data(),
                          reinterpret_array<UInt32P>(cN),
                          RecWord<MIS_WORDS>(rec, kWord_cosi), sampleV);
                StoreWord(m_ps->arena->misWords.data(),
                          select(specular, UInt32P(1u), UInt32P(0u)),
                          RecWord<MIS_WORDS>(rec, kWord_bits), sampleV);

                // Russian roulette.
                const MaskP rrActive = sampleV && ((mL.v + Int32P(1)) > Int32P(RR_START));
                const MaskP kill = rrActive && (rrR < m_urr);
                const SWCSpectrumP newBeta = core.beta * f;
                core.beta = select(rrActive && !kill,
                                   newBeta * divp(FloatP(1.f), rrR), newBeta);
                alive &= !(sampleV && !(m_sample.pdf > FloatP(0.f)));
                alive &= !kill;

                // Spawn the next ray.
                const Point3fP surfSpawn =
                    OffsetRay(ev.hit.p, ev.hit.ngeo, m_sample.wi, FloatP(EPS_RAY));
                const Point3fP medSpawn = ev.hit.p + m_sample.wi * FloatP(EPS_RAY);
                core.o = select(alive, select(mediumEvent, medSpawn, surfSpawn),
                                core.o);
                core.d = select(alive, m_sample.wi, core.d);
                core.mint = FloatP(EPS_RAY);
                core.maxt = FloatP(INF_F);
                core.time = FloatP(0.f);
                if (m_c.hasMedia)
                    core.medium =
                        select(mediumEvent, core.medium,
                               select(hitMask, SwapMedium(ev.hit, m_sample.wi),
                                      core.medium));

                // Advance registers to vertex.
                mL.prevPdfR = select(writeV, pdfR, mL.prevPdfR);
                mL.prevCosi = select(writeV, cN, mL.prevCosi);
                mL.prevP = select(writeV, pNew, mL.prevP);
                mL.prevN = select(writeV, ev.dg.n, mL.prevN);
                mL.prevRecvHasN =
                    select(writeV, RecvHasNormal(hitMask, ev.bsdf), mL.prevRecvHasN);
                m_wrote = writeV;
            }

            // RR is folded into OnVertex.
            void Survival(LaneCore &) {}

            // Bump count on lanes that wrote a vertex this step.
            void PostVertex(LaneCore &, const Event &)
            {
                mL.v = select(m_wrote, mL.v + Int32P(1), mL.v);
                m_wrote = MaskP(false);
            }

            EyeLanes &Tail() { return mL; }
            void SetTail(const EyeLanes &t) { mL = t; }

        private:
            const WalkCtx &m_c;
            PathStore *m_ps;
            SampleTable *m_tbl;
            EyeLanes mL;
            MaskP m_mediumEvent = MaskP(false);
            MaskP m_wrote = MaskP(false);
            BSDFSampleP m_sample;
            FloatP m_urr = FloatP(0.f);
        };

        // Light store walker.
        class BidirLightConsumer
        {
        public:
            using Lanes = LightLanes;

            BidirLightConsumer(const WalkCtx &c, PathStore *ps)
                : m_c(c), m_ps(ps)
            {
                mL = LightLanes::Fresh();
                mL.core.alive = MaskP(false);
            }

            Int32P Bounce(const LaneCore &) const { return mL.v; }
            // depthBase = K * (1 + lpSlot), K = MAX_DEPTH + 1.
            Int32P DepthBase(const LaneCore &) const
            {
                const Int32P K(int(MAX_DEPTH) + 1);
                return K * (Int32P(1) + Int32P(mL.lpSlot));
            }

            // Light subpaths accumulate no s = 0 emission.
            void OnPassThrough(LaneCore &, const HitP &, const MaskP &) {}

            void OnEvent(LaneCore &core, const Event &ev)
            {
                m_mediumEvent = ev.scatter;
                core.alive &= !(core.alive && ev.escape);
            }

            // Build the emitter origin and set up first ray.
            MaskP BuildOrigin(const MaskP &fill, const SamplerSource &src,
                              uint32_t slot,
                              const LightPtr &selLight, const UInt32P &selIdx,
                              const FloatP &selPdf)
            {
                const MaskP la = fill && (m_c.nLights > 0) &&
                                 (m_c.lightDepth > 0);
                if (!any(la))
                    return MaskP(false);

                const UInt32P lo = mL.loBase;
                const Point2fP uPos = src.Get2D(lo + UInt32P(1u));
                const FloatP uComp0 = src.Get1D(lo + UInt32P(3u));
                const Point2fP uDir = src.Get2D(lo + UInt32P(4u));

                BSDFPtr ebsdf(nullptr);
                DifferentialGeometryP edg;
                FloatP pdfArea(0.f);
                SWCSpectrumP Le(0.f);
                const MaskP okOrigin = selLight->Sample_Le(
                    mL.core.sw, uPos.x(), uPos.y(), uComp0, &ebsdf, &edg, &pdfArea,
                    &Le, la);

                const Vector3fP nVec(edg.n.x(), edg.n.y(), edg.n.z());
                BSDFSampleP s0;
                ebsdf->SampleF(mL.core.sw, nVec, edg, uDir.x(), uDir.y(), FloatP(0.f),
                               &s0, TransportMode::Importance, okOrigin);
                const Vector3fP wi0 = s0.wi;
                const FloatP pdfDir = s0.pdf;
                const MaskP ok = okOrigin && (pdfDir > FloatP(0.f)) &&
                                 (pdfArea > FloatP(0.f));

                const FloatP aF0 = selPdf * pdfArea;
                const FloatP cos0 = abs(dot(wi0, edg.n));
                const FloatP f0 = cos0 * divp(FloatP(1.f), pdfDir);
                const FloatP cosi0 = abs(dot(nVec, edg.ng));
                const FloatP coso0 = abs(dot(wi0, edg.ng));

                // Write origin vertex 0.
                VertexGeomP g;
                g.p = edg.p;
                g.n = edg.n;
                g.ng = edg.ng;
                g.wb = nVec;
                g.dp_du = edg.dp_du;
                g.dp_dv = edg.dp_dv;
                g.dp_ds = edg.dp_ds;
                g.dp_dt = edg.dp_dt;
                g.uv = Point2fP(edg.uv_u, edg.uv_v);
                g.flux = select(ok, Le * divp(FloatP(1.f), aF0), SWCSpectrumP(0.f));
                g.k = FloatP(0.f);
                g.matID = UInt32P(0u); // origin BSDF is the emitter
                // Origin medium.
                const UInt32P med = enoki::gather<UInt32P>(
                    m_c.lightMedium.data(), selIdx, fill);
                g.medID = UInt32P(LUX2_MEDIUM_CLEAR);
                g.medFront = med;
                g.medBack = med;
                const UInt32P rec0 = RecIdx(*m_ps, mL.path, Int32P(0), ok);
                StoreGeom(m_ps->arena->geomWords.data(), g, rec0, ok);

                VertexMisP mv;
                mv.dAW = select(ok, aF0, FloatP(0.f));
                mv.dARW = mv.dAW; // overwritten when vertex 1 samples
                mv.cosi = cosi0;
                mv.coso = coso0;
                mv.d2 = FloatP(0.f);
                mv.rr = min(FloatP(1.f), max(m_c.lightRR, f0));
                mv.rrR = min(FloatP(1.f),
                             max(m_c.eyeRR, f0 * cosi0 * divp(FloatP(1.f), coso0)));
                mv.bits = UInt32P(0u);
                StoreMis(m_ps->arena->misWords.data(), mv, rec0, ok);

                // Record the selected light index.
                {
                    uint32_t okA[PACKET_WIDTH], idxA[PACKET_WIDTH], pathA[PACKET_WIDTH];
                    enoki::store_unaligned(okA, select(ok, FloatP(1.f), FloatP(0.f)));
                    enoki::store_unaligned(idxA, selIdx);
                    enoki::store_unaligned(pathA, mL.path);
                    for (size_t i = 0; i < PACKET_WIDTH; ++i)
                        if (okA[i])
                            m_ps->lightIdx[pathA[i]] = idxA[i];
                }

                // First ray out of the emitter.
                mL.core.o = select(fill, edg.p, mL.core.o);
                mL.core.d = select(fill, wi0, mL.core.d);
                mL.core.mint = FloatP(EPS_RAY);
                mL.core.maxt = FloatP(INF_F);
                mL.core.time = FloatP(0.f);
                mL.core.medium = select(fill, med, mL.core.medium);
                mL.core.beta = select(ok, g.flux * f0, mL.core.beta);
                mL.core.alive = select(fill, ok, mL.core.alive);

                mL.v = select(fill, select(ok, Int32P(1), Int32P(0)), mL.v);
                mL.prevPdf = select(ok, pdfDir, mL.prevPdf);
                mL.prevCoso = select(ok, coso0, mL.prevCoso);
                mL.prevP = select(ok, edg.p, mL.prevP);
                mL.prevN = select(ok, edg.n, mL.prevN);
                return ok;
            }

            void OnVertex(const SamplerSource &src, LaneCore &core, const Event &ev)
            {
                MaskP &alive = core.alive;
                const MaskP mediumEvent = m_mediumEvent;
                const MaskP hitMask = ev.surface;
                const Vector3fP wi = -core.d; // back toward the light
                const MaskP validVtx = (hitMask | mediumEvent) && alive;
                if (!any(validVtx))
                    return;

                alive &= !(validVtx && mL.v >= Int32P(m_c.lightDepth));
                const MaskP writeV = validVtx && alive;
                if (!any(writeV))
                    return;

                const Point3fP pNew = ev.hit.p;
                const Vector3fP dv = pNew - mL.prevP;
                const FloatP d2 = dot(dv, dv);
                const FloatP cB = select(mediumEvent, FloatP(1.f),
                                         abs(dot(wi, ev.dg.ng))); // v.cosi

                // Write vertex.
                VertexGeomP g = BuildGeom(ev, writeV, core.beta);
                g.wb = wi;
                g.medID = select(mediumEvent, core.medium, UInt32P(LUX2_MEDIUM_CLEAR));
                g.medFront = select(mediumEvent, core.medium, ev.hit.mediumFrontID);
                g.medBack = select(mediumEvent, core.medium, ev.hit.mediumBackID);
                const UInt32P rec = RecIdx(*m_ps, mL.path, mL.v, writeV);
                StoreGeom(m_ps->arena->geomWords.data(), g, rec, writeV);

                // dAW[v] = pdf[v-1] * cosi / d2.
                VertexMisP mv;
                mv.dAW = select(writeV, mL.prevPdf * cB * divp(FloatP(1.f), d2),
                                FloatP(0.f));
                mv.dARW = FloatP(0.f); // patched at step v+1
                mv.cosi = cB;
                mv.coso = FloatP(0.f); // set after sampling
                mv.d2 = d2;
                mv.rr = FloatP(1.f);
                mv.rrR = FloatP(1.f);
                mv.bits = UInt32P(0u);
                StoreMis(m_ps->arena->misWords.data(), mv, rec, writeV);

                // Patch previous vertex d2 now; dARW after sampling.
                const MaskP hasPrevIdx = writeV && (mL.v >= Int32P(1));
                const UInt32P recPrev =
                    RecIdx(*m_ps, mL.path, mL.v - Int32P(1), hasPrevIdx);
                StoreWord(m_ps->arena->misWords.data(),
                          reinterpret_array<UInt32P>(d2),
                          RecWord<MIS_WORDS>(recPrev, kWord_d2), hasPrevIdx);

                // directPdf1 at vertex 1: direct sampling of light[0] from vertex 1.
                const MaskP v1 = writeV && eq(mL.v, Int32P(1));
                if (any(v1))
                    PatchDirectPdf1(src, ev, v1);

                const MaskP sampleV =
                    writeV && ((mL.v + Int32P(1)) < Int32P(m_c.lightDepth));

                VertexP vtx;
                vtx.valid = sampleV;
                vtx.isMedium = mediumEvent;
                vtx.p = pNew;
                vtx.dg = ev.dg;
                vtx.wo = wi;
                vtx.k = ev.k;
                vtx.bsdf = ev.bsdf;
                vtx.med = ev.med;

                // LightBounce(loBase, s) with s == mL.v: loBase + 6 + (v-1)*4.
                const UInt32P dim =
                    mL.loBase + UInt32P(6u) +
                    (UInt32P(mL.v) - UInt32P(1u)) * UInt32P(4u);
                const Point2fP u2bsdf = src.Get2D(dim);
                const FloatP ucomp = src.Get1D(dim + UInt32P(2u));
                m_urr = src.Get1D(dim + UInt32P(3u));

                SampleVertex(vtx, core.sw, u2bsdf.x(), u2bsdf.y(), ucomp, &m_sample,
                             sampleV, TransportMode::Importance);
                const MaskP specular =
                    hitMask && has_flag(m_sample.sampledType, BSDFType::Specular);
                const MaskP sampOk = sampleV && (m_sample.pdf > FloatP(0.f));

                BSDFEvalP ev2 = ZeroEval();
                FloatP vtxCos(0.f);
                EvalVertex(vtx, core.sw, m_sample.wi, &ev2, &vtxCos,
                           sampleV && !specular, TransportMode::Importance);

                const FloatP pdf = m_sample.pdf;
                const FloatP pdfR = select(specular, m_sample.pdf, ev2.pdfRev);
                const FloatP cN = select(mediumEvent, FloatP(1.f),
                                         abs(dot(m_sample.wi, ev.dg.ng))); // v.coso

                // Patch prev vertex dARW[v-1] = pdfR[v] * coso[v-1] / d2 (sampOk).
                StoreWord(m_ps->arena->misWords.data(),
                          reinterpret_array<UInt32P>(
                              select(sampOk, pdfR * mL.prevCoso * divp(FloatP(1.f), d2),
                                     FloatP(0.f))),
                          RecWord<MIS_WORDS>(recPrev, kWord_dARW), hasPrevIdx);

                const FloatP f = m_sample.f;
                const FloatP rrR = min(
                    FloatP(1.f), max(m_c.eyeRR, f * cB * divp(FloatP(1.f), cN)));
                const FloatP rr = min(FloatP(1.f), max(m_c.lightRR, f));
                StoreWord(m_ps->arena->misWords.data(),
                          reinterpret_array<UInt32P>(rr),
                          RecWord<MIS_WORDS>(rec, kWord_rr), sampleV);
                StoreWord(m_ps->arena->misWords.data(),
                          reinterpret_array<UInt32P>(rrR),
                          RecWord<MIS_WORDS>(rec, kWord_rrR), sampleV);
                StoreWord(m_ps->arena->misWords.data(),
                          reinterpret_array<UInt32P>(cN),
                          RecWord<MIS_WORDS>(rec, kWord_coso), sampleV);
                StoreWord(m_ps->arena->misWords.data(),
                          select(specular, UInt32P(1u), UInt32P(0u)),
                          RecWord<MIS_WORDS>(rec, kWord_bits), sampleV);

                // Russian roulette uses light.rr (forward).
                const MaskP rrActive = sampleV && ((mL.v + Int32P(1)) > Int32P(RR_START));
                const MaskP kill = rrActive && (rr < m_urr);
                const SWCSpectrumP newBeta = core.beta * f;
                core.beta =
                    select(rrActive && !kill, newBeta * divp(FloatP(1.f), rr), newBeta);
                alive &= !(sampleV && !(m_sample.pdf > FloatP(0.f)));
                alive &= !kill;

                const Point3fP surfSpawn =
                    OffsetRay(ev.hit.p, ev.hit.ngeo, m_sample.wi, FloatP(EPS_RAY));
                const Point3fP medSpawn = ev.hit.p + m_sample.wi * FloatP(EPS_RAY);
                core.o = select(alive, select(mediumEvent, medSpawn, surfSpawn),
                                core.o);
                core.d = select(alive, m_sample.wi, core.d);
                core.mint = FloatP(EPS_RAY);
                core.maxt = FloatP(INF_F);
                core.time = FloatP(0.f);
                if (m_c.hasMedia)
                    core.medium =
                        select(mediumEvent, core.medium,
                               select(hitMask, SwapMedium(ev.hit, m_sample.wi),
                                      core.medium));

                mL.prevPdf = select(writeV, pdf, mL.prevPdf);
                mL.prevCoso = select(writeV, cN, mL.prevCoso);
                mL.prevP = select(writeV, pNew, mL.prevP);
                mL.prevN = select(writeV, ev.dg.n, mL.prevN);
                m_wrote = writeV;
            }

            void Survival(LaneCore &) {}

            void PostVertex(LaneCore &, const Event &)
            {
                mL.v = select(m_wrote, mL.v + Int32P(1), mL.v);
                m_wrote = MaskP(false);
            }

            LightLanes &Tail() { return mL; }
            void SetTail(const LightLanes &t) { mL = t; }

        private:
            // directPdf1 = area-measure density of light[0] seen from vertex 1,
            // including the lsD selection pdf.
            void PatchDirectPdf1(const SamplerSource &, const Event &ev,
                                 const MaskP &v1)
            {
                // Origin position/normal and the selected light index.
                // Only p/n/ng of the emitter vertex are used.
                const EmitVertexData o =
                    LoadEmitVertex(*m_ps, mL.path, Int32P(0), v1);
                const uint32_t *lid = m_ps->lightIdx.data();
                UInt32P selIdx(PACKET_WIDTH, 0u);
                {
                    uint32_t pathA[PACKET_WIDTH], idxA[PACKET_WIDTH], mkA[PACKET_WIDTH];
                    enoki::store_unaligned(pathA, mL.path);
                    enoki::store_unaligned(mkA, select(v1, FloatP(1.f), FloatP(0.f)));
                    for (size_t i = 0; i < PACKET_WIDTH; ++i)
                        idxA[i] = mkA[i] > 0.f ? lid[pathA[i]] : 0u;
                    selIdx = enoki::load_unaligned<UInt32P>(idxA);
                }
                const LightPtr selLight =
                    enoki::gather<LightPtr>(m_c.lightPtrs.data(), selIdx, v1);
                const FloatP pD = enoki::gather<FloatP>(m_c.lsD->pSel.data(), selIdx,
                                                        v1);
                // Vertex 1 may be a scatter so its normal is ev.dg.n
                const MaskP recvHasN = RecvHasNormal(ev.surface, ev.bsdf);
                const FloatP pdfSolid =
                    selLight->Pdf_L(ev.hit.p, ev.dg.n, o.p, o.dg.n, v1, recvHasN) * pD;
                const FloatP directPdf1 =
                    ConvertDensity(ev.hit.p, pdfSolid, o.p, o.dg.ng);
                {
                    float dA[PACKET_WIDTH];
                    uint32_t mkA[PACKET_WIDTH], pathA[PACKET_WIDTH];
                    enoki::store_unaligned(dA, directPdf1);
                    enoki::store_unaligned(mkA, select(v1, FloatP(1.f), FloatP(0.f)));
                    enoki::store_unaligned(pathA, mL.path);
                    for (size_t i = 0; i < PACKET_WIDTH; ++i)
                        if (mkA[i] > 0.f)
                            m_ps->directPdf1[pathA[i]] = dA[i];
                }
            }

            const WalkCtx &m_c;
            PathStore *m_ps;
            LightLanes mL;
            MaskP m_mediumEvent = MaskP(false);
            MaskP m_wrote = MaskP(false);
            BSDFSampleP m_sample;
            FloatP m_urr = FloatP(0.f);
        };

    } // namespace wf
} // namespace lux2

#endif // LUX2_INTEGRATORS_CONSUMERS_BIDIRECTIONAL_CONSUMER_H
