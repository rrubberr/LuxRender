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

namespace lux2
{
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
                                          FloatP *alphaOut) const
    {
        const EmbreeScene *embree = scene.GetEmbree();
        const BsdfPtrTable &table = scene.GetBsdfTable();
        const std::vector<std::shared_ptr<Light>> &lights = scene.GetLights();
        const int nLights = static_cast<int>(lights.size());
        const FloatP invNLights =
            nLights > 0 ? FloatP(1.f / float(nLights)) : FloatP(0.f);

        const bool FULL_MIS = true; // full MIS
        const bool NEE = m_directLightSampling;
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
            // Fixed 8-dim sample budget for this bounce.
            sampler.Advance();
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
                            envW = select(specularBounce || depth == Int32P(0),
                                          FloatP(1.f), misWeight(prevPdf, pdfLdir));
                        }
                        L = select(la, L + envW * throughput * light->Le(ray, la), L);
                    }
                }
                // Escaped to the background.
                alpha = select(miss && depth == Int32P(0), FloatP(0.f), alpha);
                alive &= !miss;
            }

            // Hit!
            const MaskP hitMask = alive && hit.hit;
            if (any(hitMask))
            {
                // A primary ray that lands on geometry is opaque coverage.
                alpha = select(hitMask && depth == Int32P(0), FloatP(1.f), alpha);

                const Point3fP p = hit.p;
                const Normal3fP ng = hit.ngeo;
                const MaskP entering = dot(ray.d, ng) < FloatP(0.f);
                // lux never flips the shading normal to face the ray: BxDFs
                // work in a frame built on dgShading.nn and side logic runs
                // through ng. Here, n is the interpolated shading normal.
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

                // Emission on hit.
                MaskP emCount = hitMask && (hit.lightID >= Int32P(0));
                if (!FULL_MIS && NEE)
                    emCount = emCount && specularBounce;
                if (any(emCount))
                {
                    const int nArea = scene.AreaLightCount();
                    for (int a = 0; a < nArea; ++a)
                    {
                        const Light *al = scene.GetAreaLight(a);
                        const MaskP la = emCount && (hit.lightID == Int32P(a));
                        if (!any(la))
                            continue;
                        FloatP emW(1.f);
                        if (FULL_MIS && NEE)
                        {
                            const FloatP pdfLEmitter =
                                al->Pdf_L(prevP, prevN, p, n, la) * invNLights;
                            emW = select(specularBounce, FloatP(1.f),
                                         misWeight(prevPdf, pdfLEmitter));
                        }
                        L = select(la, L + emW * throughput * al->Le(dg, sw, wo, la), L);
                    }
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
                        const MaskP la = hitAlive && (lightIdx == UInt32P(uint32_t(k)));
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
                        const FloatP tmax = enoki::any(light->IsInfinite())
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

        for (int py = tile.y0; py < tile.y1; ++py)
        {
            for (int px = tile.x0; px < tile.x1; px += int(PACKET_WIDTH))
            {
                const FloatP x = FloatP(float(px)) + lane + FloatP(0.5f);
                const FloatP y = FloatP(float(py)) + FloatP(0.5f);

                const uint64_t seedOffset =
                    (sampleBasis * 0x9E3779B97F4A7C15ull) ^
                    (uint64_t(px) * 0xBF58476D1CE4E5B9ull) ^
                    (uint64_t(py) * 0x94D049BB133111EBull);
                sampler.Seed(seedOffset, PACKET_WIDTH);

                for (uint32_t s = 0; s < spp; ++s)
                {
                    SpectrumWavelengthsP sw;
                    sw.Sample(sampler.Next1D());

                    RayP ray;
                    FloatP weight;
                    camera.GenerateRay(x, y, FloatP(0.f), &ray, &weight);
                    ray.wavelengths = sw.w;

                    FloatP alpha;
                    const SWCSpectrumP L = WalkPath(scene, sampler, ray, sw, &alpha);
#ifdef PATH_DEBUG
                    {
                        float La[PACKET_WIDTH], lu[PACKET_WIDTH], aa[PACKET_WIDTH];
                        enoki::store_unaligned(La, L);
                        enoki::store_unaligned(lu, SWCY(L, sw) * weight);
                        enoki::store_unaligned(aa, alpha);
                        for (size_t i = 0; i < PACKET_WIDTH; ++i)
                        {
                            g_fb_Lsum += La[i];
                            g_fb_LumSum += lu[i];
                            g_fb_alphaSum += aa[i] > 0.f ? 1.0 : 0.0;
                            g_fb_n += 1.0;
                            if (La[i] > g_fb_maxL)
                                g_fb_maxL = La[i];
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
                    dest.Splat(x, y, L, sw, alpha, weight, 0);
                } // spp
            }
        }
#ifdef PATH_DEBUG
        fprintf(stderr, "[FB AGG] n=%.0f Lsum=%.4g maxL=%.4g LumSum=%.4g alphaCount=%.0f meanLumOverAlpha=%.4g\n",
                g_fb_n, g_fb_Lsum, g_fb_maxL, g_fb_LumSum, g_fb_alphaSum,
                g_fb_alphaSum > 0 ? g_fb_LumSum / g_fb_alphaSum : 0.0);
        g_fb_Lsum = g_fb_LumSum = g_fb_alphaSum = g_fb_n = g_fb_maxL = 0;
        g_fb_hdr = false;
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
        // "lightstrategy" is accepted and ignored.
        if (p)
            p->EraseString("lightstrategy");

        return std::make_shared<PathIntegrator>(
            maxDepth, rrContinueProb, rrStrategy,
            includeEnvironment, directLightSampling);
    }

    LUX2_REGISTER_SURFACE_INTEGRATOR(PathIntegrator, "path");

} // namespace lux2
