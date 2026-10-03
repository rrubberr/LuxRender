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

#include "integrators/path.h"

#include "core/bsdf_call.h"
#include "core/bsdfptr_table.h"
#include "core/color.h"
#include "core/dynload.h"
#include "core/embree2.h"
#include "core/light.h"
#include "core/math.h"
#include "core/register.h"
#include "core/scene.h"

#include <enoki/array.h>

#include <cstdio>
#include <limits>

#if defined(PATH_DEBUG) || defined(LAMP_DEBUG)
#include <atomic>
#endif

namespace lux2
{
#ifdef PATH_DEBUG
    namespace
    {
        // First pixel dump flag.
        std::atomic<bool> g_fb_hdr{false};
    } // namespace
#endif

#ifdef LAMP_DEBUG
    namespace
    {
        // Caps on lamp packet dumps.
        std::atomic<long> g_lamp_edgeDumps{0};
        std::atomic<long> g_lamp_leDumps{0};
    } // namespace
#endif

    namespace
    {
        // Power heuristic for combining two sampling strategies.
        inline FloatP misWeight(const FloatP &aIn, const FloatP &bIn)
        {
            const FloatP a = aIn * aIn;
            const FloatP b = bIn * bIn;
            return select(a > FloatP(0.f), a / (a + b), FloatP(0.f));
        }

        // Geometric offset.
        inline Point3fP OffsetRay(const Point3fP &p, const Normal3fP &ng,
                                  const Vector3fP &d, FloatP k)
        {
            const Normal3fP s = select(dot(d, ng) > FloatP(0.f), ng, -ng);
            return p + Vector3fP(s.x(), s.y(), s.z()) * k;
        }
    } // namespace

    void PathIntegrator::Start(const Scene &scene)
    {
        m_scene = &scene;
    }

    SWCSpectrumP PathIntegrator::WalkPath(const Scene &scene, Sampler &sampler,
                                          const RayP &primary,
                                          const SpectrumWavelengthsP &sw,
                                          FloatP *alphaOut,
                                          SWCSpectrumP *lemOut) const
    {
        const EmbreeScene *embree = scene.GetEmbree();
        const BsdfPtrTable &table = scene.GetBsdfTable();
        const std::vector<std::shared_ptr<Light>> &lights = scene.GetLights();
        const int nLights = static_cast<int>(lights.size());
        const FloatP invNLights =
            nLights > 0 ? FloatP(1.f / float(nLights)) : FloatP(0.f);

        const bool FULL_MIS = (m_lightMode == LightMode::MIS);
        const bool NEE =
            (m_lightMode != LightMode::BSDF) && m_directLightSampling;
        const bool includeEnv = m_includeEnvironment;
        const int maxDepth = m_maxDepth;
        const bool rrEfficiency = (m_rrStrategy == "efficiency");
        const FloatP rrProb = FloatP(m_rrContinueProb);

        const FloatP INF = std::numeric_limits<float>::infinity();

        RayP ray = primary;
        ray.alive = MaskP(true);

        SWCSpectrumP L(0.f);
        SWCSpectrumP throughput(1.f);
        // Coverage starts transparent.
        FloatP alpha(0.f);

        Int32P depth(0);
        MaskP specularBounce(true);
        MaskP alive(true);
        Point3fP prevP = ray.o;
        Normal3fP prevN(0.f, 0.f, 0.f);
        FloatP prevPdf(0.f);

        while (any(alive))
        {
            const Point2fP u2bsdf = sampler.Next2D();
            const FloatP ucomp = sampler.Next1D();
            const FloatP urr = sampler.Next1D();
            const FloatP ulight = sampler.Next1D();
            const Point2fP u2lpos = sampler.Next2D();
            const FloatP ulightcomp = sampler.Next1D();

            ray.alive = alive;
            HitP hit;
            embree->Intersect(ray, hit, Coherent::No);

            // Environment / infinite light
            const MaskP miss = alive && !hit.hit;
            if (any(miss))
            {
                // Broadcast the scalar params to lane masks.
                const MaskP envOn = MaskP(includeEnv);
                const MaskP deepper = depth > Int32P(0);
                MaskP envCount;
                if (FULL_MIS)
                    envCount = miss && (envOn || deepper);
                else
                    envCount = miss && (MaskP(!NEE) || ((envOn || deepper) && specularBounce));
                if (nLights > 0 && any(envCount))
                {
                    for (int k = 0; k < nLights; ++k)
                    {
                        const Light *light = lights[k].get();
                        if (!light->IsInfinite())
                            continue;
                        const MaskP la = envCount;
                        if (!any(la))
                            continue;
                        FloatP envW(1.f);
                        if (FULL_MIS && NEE)
                        {
                            const FloatP pdfLdir =
                                light->Pdf_L(prevP, prevN, Point3fP(0.f),
                                             Normal3fP(0.f), la) *
                                invNLights;
                            envW = select(specularBounce || eq(depth, Int32P(0)),
                                          FloatP(1.f), misWeight(prevPdf, pdfLdir));
                        }
                        L = select(la, L + envW * throughput * light->Le(ray, la), L);
                    }
                }
                // Escaped to the background.
                alpha = select(miss && eq(depth, Int32P(0)), FloatP(0.f), alpha);
                alive &= !miss;
            }

            // Hit!
            const MaskP hitMask = alive && hit.hit;
            if (any(hitMask))
            {
                // A primary ray that lands on geometry is opaque coverage.
                alpha = select(hitMask && eq(depth, Int32P(0)), FloatP(1.f), alpha);

                const Point3fP p = hit.p;
                const Normal3fP ng = hit.ngeo;
                const MaskP entering = dot(ray.d, ng) < FloatP(0.f);
                // lux never flips the shading normal to face the ray, BxDFs
                // work in a frame built on dgShading.nn and side logic runs
                // through ng.
                const Normal3fP n = hit.sh_n;

                DifferentialGeometryP dg;
                dg.p = p;
                dg.n = n;
                dg.ng = ng;
                dg.entering = entering;
                dg.uv_u = hit.uv.x();
                dg.uv_v = hit.uv.y();
                // Shading frame from the ShadeHit UV gradient solve.
                dg.dp_du = hit.dp_du;
                dg.dp_dv = hit.dp_dv;
                dg.dp_ds = hit.dp_ds;
                dg.dp_dt = hit.dp_dt;

                const Vector3fP wo = -ray.d;
                const BSDFPtr bsdf = table.Gather(hit.matID, hitMask);

#ifdef LAMP_DEBUG
                // Primary packet straddling the lamp silhouette.
                {
                    const MaskP primary = hitMask && eq(depth, Int32P(0));
                    const MaskP lamp = primary && (hit.lightID >= Int32P(0));
                    if (any(lamp) && any(primary && !lamp) &&
                        g_lamp_edgeDumps.fetch_add(1) < 8)
                    {
                        float t[PACKET_WIDTH], prim[PACKET_WIDTH];
                        uint32_t mat[PACKET_WIDTH];
                        int32_t lid[PACKET_WIDTH];
                        enoki::store_unaligned(t, hit.t);
                        enoki::store_unaligned(mat, hit.matID);
                        enoki::store_unaligned(lid, hit.lightID);
                        enoki::store_unaligned(prim,
                                               select(primary, FloatP(1.f), FloatP(0.f)));
                        fprintf(stderr, "[LAMP EDGE packet] lane: t matID lightID\n");
                        for (size_t i = 0; i < PACKET_WIDTH; ++i)
                            fprintf(stderr, "  lane %zu: %8.5f %5u %4d%s\n",
                                    i, t[i], mat[i], lid[i],
                                    prim[i] > 0.f ? "" : " (inactive)");
                    }
                }
#endif

                // Emission on hit.
                MaskP emCount = hitMask && (hit.lightID >= Int32P(0));
                if (!FULL_MIS && NEE)
                    emCount = emCount && specularBounce;
                SWCSpectrumP emTerm(0.f); // total emission added this bounce
#ifdef LAMP_DEBUG
                // Lane emission factors.
                SWCSpectrumP dbgLe(0.f);
                FloatP dbgEmW(0.f), dbgTput(0.f);
                MaskP dbgLa(false);
#endif
                if (any(emCount))
                {
                    const int nArea = scene.AreaLightCount();
                    for (int a = 0; a < nArea; ++a)
                    {
                        const Light *al = scene.GetAreaLight(a);
                        const MaskP la = emCount && eq(hit.lightID, Int32P(a));
                        if (!any(la))
                            continue;
                        FloatP emW(1.f);
                        if (FULL_MIS && NEE)
                        {
                            // Shading normal n.
                            const FloatP pdfLEmitter =
                                al->Pdf_L(prevP, prevN, p, n, la) * invNLights;
                            emW = select(specularBounce, FloatP(1.f),
                                         misWeight(prevPdf, pdfLEmitter));
                        }
                        const SWCSpectrumP LeRaw = al->Le(dg, sw, wo, la);
                        const SWCSpectrumP Lem = emW * throughput * LeRaw;
#ifdef LAMP_DEBUG
                        dbgLe = select(la, LeRaw, dbgLe);
                        dbgEmW = select(la, emW, dbgEmW);
                        dbgTput = select(la, throughput, dbgTput);
                        dbgLa = dbgLa || la;
#endif
                        emTerm = emTerm + select(la, Lem, SWCSpectrumP(0.f));
                        L = select(la, L + Lem, L);
                    }
                }
#ifdef LAMP_DEBUG
                // Any visible lamp lane that ended up with zero emission.
                {
                    const MaskP lampLane =
                        hitMask && eq(depth, Int32P(0)) && (hit.lightID >= Int32P(0));
                    const MaskP dead = lampLane && IsBlack(emTerm);
                    if (any(dead) && g_lamp_leDumps.fetch_add(1) < 8)
                    {
                        const int nArea = scene.AreaLightCount();
                        float ngd[PACKET_WIDTH], nd[PACKET_WIDTH],
                              em[PACKET_WIDTH], dmask[PACKET_WIDTH],
                              hmask[PACKET_WIDTH], lamask[PACKET_WIDTH],
                              leraw[PACKET_WIDTH], emw[PACKET_WIDTH],
                              tput[PACKET_WIDTH];
                        uint32_t lid[PACKET_WIDTH];
                        enoki::store_unaligned(ngd, dot(dg.ng, wo));
                        enoki::store_unaligned(nd, dot(dg.n, wo));
                        enoki::store_unaligned(em, emTerm);
                        enoki::store_unaligned(lid, hit.lightID);
                        enoki::store_unaligned(dmask,
                                               select(dead, FloatP(1.f), FloatP(0.f)));
                        enoki::store_unaligned(hmask,
                                               select(hitMask, FloatP(1.f), FloatP(0.f)));
                        enoki::store_unaligned(lamask,
                                               select(dbgLa, FloatP(1.f), FloatP(0.f)));
                        enoki::store_unaligned(leraw, dbgLe);
                        enoki::store_unaligned(emw, dbgEmW);
                        enoki::store_unaligned(tput, dbgTput);
                        fprintf(stderr,
                                "[LAMP LE packet] nArea=%d lane: hit la ng.wo n.wo LeRaw emW tput emTerm lightID\n",
                                nArea);
                        for (size_t i = 0; i < PACKET_WIDTH; ++i)
                            fprintf(stderr,
                                    "  lane %zu: %3.0f %3.0f %9.5f %9.5f %9.4g %6.3f %6.3f %9.4g %4d%s\n",
                                    i, hmask[i], lamask[i], ngd[i], nd[i],
                                    leraw[i], emw[i], tput[i], em[i], lid[i],
                                    dmask[i] > 0.f ? " <- dead" : "");
                    }
                }
#endif
                if (lemOut)
                {
                    // Visible emission only AOV.
                    const MaskP d0 = emCount && eq(depth, Int32P(0));
                    *lemOut = select(d0, *lemOut + emTerm, *lemOut);
                }

                // Depth limit.
                alive &= !(hitMask && depth >= Int32P(maxDepth));
                const MaskP hitAlive = hitMask && alive;

                // NEE.
                if (NEE && nLights > 0 && any(hitAlive))
                {
                    const UInt32P lightIdx =
                        min(UInt32P(uint32_t(nLights - 1)),
                            UInt32P(floor(ulight * FloatP(float(nLights)))));
                    for (int k = 0; k < nLights; ++k)
                    {
                        const Light *light = lights[k].get();
                        const MaskP la = hitAlive && eq(lightIdx, UInt32P(uint32_t(k)));
                        if (!any(la))
                            continue;
                        Point3fP lightP(0.f, 0.f, 0.f);
                        Vector3fP wi(0.f, 0.f, 0.f);
                        Normal3fP lightN(0.f, 0.f, 0.f);
                        FloatP pdfPos(0.f);
                        SWCSpectrumP Le(0.f);
                        const MaskP valid =
                            light->Sample_L(sw, p, n, u2lpos.x(), u2lpos.y(),
                                            ulightcomp, &lightP, &wi, &lightN,
                                            &pdfPos, &Le, la);
                        const FloatP pdfL = pdfPos * invNLights;
                        // Offset toward the side the path arrived on.
                        const Point3fP off = OffsetRay(p, ng, -ray.d, FloatP(EPS_RAY));
                        // IsInfinite() is a scalar virtual; branch directly.
                        const FloatP tmax = light->IsInfinite()
                                                ? INF
                                                : enoki::norm(lightP - p) *
                                                      (FloatP(1.f) - FloatP(EPS_RAY));
                        RayP shadow(off, wi, FloatP(EPS_RAY), tmax, FloatP(0.f));
                        shadow.wavelengths = sw.w;
                        shadow.mask = UInt32P(0xFFFFFFFFu);
                        const MaskP tryVis = la && valid && (pdfL > FloatP(0.f));
                        const MaskP vis =
                            tryVis && !embree->Occluded(shadow, tryVis, Coherent::No);

                        BSDFEvalP ev;
                        ev.f = SWCSpectrumP(0.f);
                        ev.pdf = FloatP(0.f);
                        ev.pdfRev = FloatP(0.f);
                        // lux NEE calls bsdf->F(..., reverse=true):
                        // Radiance == reverse (wo toward the eye), no ng
                        // Jacobian in Eval.f.
                        bsdf->Eval(sw, wi, wo, dg, TransportMode::Radiance, &ev, la);

                        // ev.f carries |cos(0ᵢ)|. cosL is hemisphere agnostic.
                        // Degenerate (wi,wo) pairs are zeroed in ev.f by the BSDF side test.
                        const FloatP cosL = abs(dot(wi, n));
                        FloatP neeW(1.f);
                        if (FULL_MIS)
                            neeW = misWeight(pdfL, ev.pdf);
                        const MaskP ok = vis && (pdfL > FloatP(0.f)) && (cosL > FloatP(0.f));
                        const SWCSpectrumP Ld = select(
                            ok, neeW * throughput * Le * ev.f / max(pdfL, FloatP(EPS_DENOM)),
                            SWCSpectrumP(0.f));
                        L = L + Ld;
                    }
                }

                // BSDF sample. Eye path uses reverse=true == TransportMode::Radiance.
                BSDFSampleP sample;
                sample.wi = Vector3fP(0.f);
                sample.f = SWCSpectrumP(0.f);
                sample.pdf = FloatP(0.f);
                sample.eta = FloatP(1.f);
                sample.sampledType = UInt32P(0u);
                sample.specular = MaskP(false);
                bsdf->SampleF(sw, wo, dg, u2bsdf.x(), u2bsdf.y(), ucomp, &sample,
                              TransportMode::Radiance, hitAlive);

                alive &= !(hitAlive && (sample.pdf <= FloatP(0.f)));

                // lux applies efficiency RR to specular transmission bounces.
                const MaskP rrActive =
                    alive && (depth > Int32P(3));
                if (any(rrActive))
                {
                    const SWCSpectrumP tputF = sample.f;
                    FloatP q;
                    if (rrEfficiency)
                        q = min(FloatP(1.f), max(tputF, FloatP(0.f)));
                    else
                        q = rrProb;
                    const MaskP kill = rrActive && (q < urr);
                    throughput = select(rrActive && !kill,
                                        throughput / max(q, FloatP(EPS_DENOM)),
                                        throughput);
                    alive &= !kill;
                }

                // Advance.
                throughput = select(alive, throughput * sample.f, throughput);
                specularBounce =
                    select(hitMask, has_flag(sample.sampledType, BSDFType::Specular),
                           specularBounce);

                const Point3fP spawn = OffsetRay(p, ng, sample.wi, FloatP(EPS_RAY));
                ray = RayP(spawn, sample.wi, FloatP(EPS_RAY), INF, FloatP(0.f));
                ray.wavelengths = sw.w;
                ray.mask = UInt32P(0xFFFFFFFFu);
                depth = depth + Int32P(1);
                prevP = p;
                prevN = n;
                prevPdf = sample.pdf;
            }
        }

        *alphaOut = alpha;
        return L;
    }

    void PathIntegrator::RenderTile(const Scene &scene, const Tile &tile,
                                    Film &dest, Sampler &sampler,
                                    uint64_t sampleBasis, uint32_t spp)
    {
        const Camera &camera = scene.GetCamera();

        const FloatP lane = enoki::arange<FloatP>();

#ifdef PATH_DEBUG
        // Tile debug accumulators.
        double fb_Lsum = 0., fb_LumSum = 0., fb_alphaSum = 0., fb_n = 0., fb_maxL = 0.;
#endif
#ifdef LAMP_DEBUG
        long lamp_mixedPackets = 0;
#endif

        for (int py = tile.y0; py < tile.y1; ++py)
        {
            for (int px = tile.x0; px < tile.x1; px += int(PACKET_WIDTH))
            {
                const uint64_t seedOffset =
                    (sampleBasis * 0x9E3779B97F4A7C15ull) ^
                    (uint64_t(px) * 0xBF58476D1CE4E5B9ull) ^
                    (uint64_t(py) * 0x94D049BB133111EBull);
                sampler.Seed(seedOffset, PACKET_WIDTH);

                for (uint32_t s = 0; s < spp; ++s)
                {
                    // One sample sequence per (pixel, spp).
                    sampler.Advance();
                    const Point2fP jitter = sampler.Next2D();
                    const FloatP x = FloatP(float(px)) + lane + jitter.x();
                    const FloatP y = FloatP(float(py)) + jitter.y();

                    SpectrumWavelengthsP sw;
                    sw.Sample(sampler.Next1D());

                    RayP ray;
                    FloatP weight;
                    camera.GenerateRay(x, y, FloatP(0.f), &ray, &weight);
                    ray.wavelengths = sw.w;
#ifdef LAMP_DEBUG
                    // Primary ray mask as handed to WalkPath.
                    const UInt32P primaryMask = ray.mask;
#endif

                    FloatP alpha;
#ifdef LAMP_COVERAGE
                    SWCSpectrumP lem(0.f);
                    const SWCSpectrumP L =
                        WalkPath(scene, sampler, ray, sw, &alpha, &lem);
#else
                    const SWCSpectrumP L = WalkPath(scene, sampler, ray, sw, &alpha);
#endif
#ifdef PATH_DEBUG
                    {
                        float La[PACKET_WIDTH], lu[PACKET_WIDTH], aa[PACKET_WIDTH];
                        enoki::store_unaligned(La, L);
                        enoki::store_unaligned(lu, SWCY(L, sw) * weight);
                        enoki::store_unaligned(aa, alpha);
                        for (size_t i = 0; i < PACKET_WIDTH; ++i)
                        {
                            fb_Lsum += La[i];
                            fb_LumSum += lu[i];
                            fb_alphaSum += aa[i] > 0.f ? 1.0 : 0.0;
                            fb_n += 1.0;
                            if (La[i] > fb_maxL)
                                fb_maxL = La[i];
                        }
                        if (!g_fb_hdr && px == tile.x0 && py == tile.y0)
                        {
                            g_fb_hdr = true;
                            fprintf(stderr, "[FB first-pixel] L=[");
                            for (size_t i = 0; i < PACKET_WIDTH; ++i)
                                fprintf(stderr, "%.4g ", La[i]);
                            fprintf(stderr, "] alpha=[");
                            for (size_t i = 0; i < PACKET_WIDTH; ++i)
                                fprintf(stderr, "%.4g ", aa[i]);
                            fprintf(stderr, "] SWCY(L)*w=[");
                            for (size_t i = 0; i < PACKET_WIDTH; ++i)
                                fprintf(stderr, "%.4g ", lu[i]);
                            fprintf(stderr, "]\n");
                        }
                    }
#endif
#ifdef LAMP_DEBUG
                    {
                        // Mixed-coverage packet: some lanes hit, some missed.
                        float aa[PACKET_WIDTH];
                        enoki::store_unaligned(aa, alpha);
                        bool mixed = false;
                        for (size_t i = 1; i < PACKET_WIDTH; ++i)
                            mixed |= (aa[i] > 0.f) != (aa[0] > 0.f);
                        if (mixed && ++lamp_mixedPackets <= 8)
                        {
                            uint32_t pm[PACKET_WIDTH];
                            enoki::store_unaligned(pm, primaryMask);
                            fprintf(stderr,
                                    "[LAMP MIXED tile(%d,%d) px=%d py=%d s=%u] alpha=[",
                                    tile.x0, tile.y0, px, py, s);
                            for (size_t i = 0; i < PACKET_WIDTH; ++i)
                                fprintf(stderr, "%.4g ", aa[i]);
                            fprintf(stderr, "] primaryMask=[");
                            for (size_t i = 0; i < PACKET_WIDTH; ++i)
                                fprintf(stderr, "%08x ", pm[i]);
                            fprintf(stderr, "] o=[");
                            float ox[PACKET_WIDTH], oy[PACKET_WIDTH], oz[PACKET_WIDTH];
                            enoki::store_unaligned(ox, ray.o.x());
                            enoki::store_unaligned(oy, ray.o.y());
                            enoki::store_unaligned(oz, ray.o.z());
                            for (size_t i = 0; i < PACKET_WIDTH; ++i)
                                fprintf(stderr, "(%g,%g,%g) ", ox[i], oy[i], oz[i]);
                            fprintf(stderr, "]\n");
                        }
                    }
#endif
#ifdef LAMP_HITAOV
                    // Hit AOV encoded by wavelength 
                    //   lamp, facet normal faces eye  -> red   (630)
                    //   lamp, facet normal faces away -> blue  (450)
                    //   other geometry                -> green (530)
                    //   miss                          -> black
                    RayP probe = ray;
                    HitP ph;
                    scene.GetEmbree()->Intersect(probe, ph, Coherent::No);
                    const MaskP isLamp = ph.hit && (ph.lightID >= Int32P(0));
                    const MaskP lampFront =
                        isLamp && (dot(ph.ngeo, -probe.d) > FloatP(0.f));
                    SpectrumWavelengthsP swAov;
                    swAov.FromWavelength(select(
                        isLamp, select(lampFront, FloatP(630.f), FloatP(450.f)),
                        FloatP(530.f)));
                    const SWCSpectrumP Lout =
                        select(ph.hit, SWCSpectrumP(1.f), SWCSpectrumP(0.f));
                    dest.Splat(x, y, Lout, swAov, alpha, weight, 0);
#elif defined(LAMP_COVERAGE)
                    // Emission AOV to splat Lem with the real sw.
                    dest.Splat(x, y, lem, sw, alpha, weight, 0);
#else
                    dest.Splat(x, y, L, sw, alpha, weight, 0);
#endif
                } // spp
            }
        }
#ifdef PATH_DEBUG
        fprintf(stderr,
                "[FB AGG tile(%d,%d)] n=%.0f Lsum=%.4g maxL=%.4g LumSum=%.4g alphaCount=%.0f meanLumOverAlpha=%.4g\n",
                tile.x0, tile.y0, fb_n, fb_Lsum, fb_maxL, fb_LumSum, fb_alphaSum,
                fb_alphaSum > 0 ? fb_LumSum / fb_alphaSum : 0.0);
#endif
#ifdef LAMP_DEBUG
        if (lamp_mixedPackets > 0)
            fprintf(stderr, "[LAMP tile(%d,%d)] mixedPackets=%ld\n",
                    tile.x0, tile.y0, lamp_mixedPackets);
#endif
    }

    std::shared_ptr<SurfaceIntegrator> PathIntegrator::CreateSurfaceIntegrator(
        const PluginContext &ctx)
    {
        ParamSet *p = ctx.params;
        const int maxDepth = p ? p->FindOneInt("maxdepth", 8) : 8;
        const float rrContinueProb =
            p ? p->FindOneFloat("rrcontinueprob", 0.65f) : 0.65f;
        const std::string rrStrategy =
            p ? p->FindOneString("rrstrategy", "efficiency") : "efficiency";
        const bool includeEnvironment =
            p ? p->FindOneBool("includeenvironment", true) : true;
        const bool directLightSampling =
            p ? p->FindOneBool("directlightsampling", true) : true;
        // Debug estimator selection: "mis" (default), "nee", or "bsdf".
        const std::string lightModeStr =
            p ? p->FindOneString("lightmode", "mis") : "mis";
        PathIntegrator::LightMode lightMode = PathIntegrator::LightMode::MIS;
        if (lightModeStr == "nee")
            lightMode = PathIntegrator::LightMode::NEE;
        else if (lightModeStr == "bsdf")
            lightMode = PathIntegrator::LightMode::BSDF;
        // "lightstrategy" is accepted and ignored.
        if (p)
            p->EraseString("lightstrategy");

        return std::make_shared<PathIntegrator>(
            maxDepth, rrContinueProb, rrStrategy,
            includeEnvironment, directLightSampling, lightMode);
    }

    LUX2_REGISTER_SURFACE_INTEGRATOR(PathIntegrator, "path");

} // namespace lux2
