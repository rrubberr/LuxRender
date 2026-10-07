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

#include "lights/area.h"

#include "core/bsdf_type.h"
#include "core/dynload.h"
#include "core/math.h"
#include "core/paramset.h"
#include "core/register.h"
#include "core/texture.h"
#include "textures/constant.h"

#include <cmath>
#include <enoki/array.h>

namespace lux2
{

    AreaLight::AreaLight(std::shared_ptr<ColorTexture> Le, float gain)
        : m_Le(std::move(Le)), m_gain(gain) {}

    uint32_t AreaLight::flags() const
    {
        return uint32_t(BSDFType::DiffuseReflection);
    }

    void AreaLight::BindGeometry(const std::vector<TriangleDesc> &tris)
    {
        m_triCount = static_cast<std::uint32_t>(tris.size());
        m_tris.resize(m_triCount);
        m_cdf.resize(m_triCount);

        m_totalArea = 0.f;
        for (std::uint32_t i = 0; i < m_triCount; ++i)
        {
            const TriangleDesc &t = tris[i];
            AreaTriangle<float> &at = m_tris[i];
            at.v0 = t.v0;
            at.v1 = t.v1;
            at.v2 = t.v2;
            at.n0 = t.n0;
            at.n1 = t.n1;
            at.n2 = t.n2;

            // Triangle area = 0.5 * |(v1-v0) x (v2-v0)|.
            const Vector3f e1 = t.v1 - t.v0;
            const Vector3f e2 = t.v2 - t.v0;
            const Vector3f cr = enoki::cross(e1, e2);
            const float area = 0.5f * enoki::sqrt(enoki::dot(cr, cr));
            m_totalArea += area;
            m_cdf[i] = m_totalArea;
        }
    }

    SWCSpectrumP AreaLight::Le(const RayP &ray, MaskP active) const
    {
        // TODO: n=(0,0,1) and uv=(0,0). Correct for constant Le textures;
        // wrong for UV-mapped or direction dependent emitters (need a real
        // ShadeHit dg at emitter intersection).
        DifferentialGeometryP dg;
        dg.p = ray(ray.maxt);
        dg.n = Normal3fP(FloatP(0.f), FloatP(0.f), FloatP(1.f));
        dg.ng = dg.n;
        dg.uv_u = FloatP(0.f);
        dg.uv_v = FloatP(0.f);

        // Expand to a full sw so the color texture can reconstruct its spectrum.
        SpectrumWavelengthsP sw;
        sw.FromWavelength(ray.wavelengths);
        return m_Le->Evaluate(dg, sw, active) * FloatP(m_gain);
    }

    SWCSpectrumP AreaLight::Le(const DifferentialGeometryP &dg,
                               const SpectrumWavelengthsP &sw,
                               const Vector3fP &wo,
                               MaskP active) const
    {
        // One sided emission is a geometric property, so test the face
        // normal.
        const MaskP front = active && (dot(dg.ng, wo) > FloatP(0.f));
        return select(front, m_Le->Evaluate(dg, sw, front) * FloatP(m_gain),
                      SWCSpectrumP(0.f));
    }

    MaskP AreaLight::Sample_L(const SpectrumWavelengthsP &sw,
                              const Point3fP &p, const Normal3fP &n,
                              const FloatP &u0, const FloatP &u1, const FloatP &u2,
                              Point3fP *lightP, Vector3fP *wi, Normal3fP *lightN,
                              FloatP *pdf, SWCSpectrumP *LeOut,
                              MaskP active) const
    {
        if (m_triCount == 0)
        {
            enoki::masked(*pdf, active) = FloatP(0.f);
            return MaskP(false);
        }

        // Pick a triangle by searching the area CDF for u0 * totalArea.
        const FloatP target = u0 * FloatP(m_totalArea);
        UInt32P triIdx(0u);
        {
            // Advance a cursor over the CDF.
            UInt32P lo(0u), hi(UInt32P(m_triCount - 1u));
            while (enoki::any(lo < hi))
            {
                const UInt32P mid = (lo + hi) >> 1;
                const FloatP cdfMid =
                    enoki::gather<FloatP>(m_cdf.data(), mid, active);
                const MaskP goRight = cdfMid < target;
                lo = enoki::select(goRight, mid + UInt32P(1u), lo);
                hi = enoki::select(goRight, hi, mid);
            }
            triIdx = lo;
        }

        // Gather the selected triangle's vertices and normals.
        constexpr size_t kStride = sizeof(AreaTriangle<float>);
        const float *base = reinterpret_cast<const float *>(m_tris.data());
        const float *memberBase[6] = {
            base + offsetof(AreaTriangle<float>, v0) / sizeof(float),
            base + offsetof(AreaTriangle<float>, v1) / sizeof(float),
            base + offsetof(AreaTriangle<float>, v2) / sizeof(float),
            base + offsetof(AreaTriangle<float>, n0) / sizeof(float),
            base + offsetof(AreaTriangle<float>, n1) / sizeof(float),
            base + offsetof(AreaTriangle<float>, n2) / sizeof(float)};
        auto gatherV = [memberBase](int member, UInt32P idx, MaskP m)
        {
            const float *p = memberBase[member];
            return enoki::Array<FloatP, 3>(
                enoki::gather<FloatP, kStride>(p + 0, idx, m),
                enoki::gather<FloatP, kStride>(p + 1, idx, m),
                enoki::gather<FloatP, kStride>(p + 2, idx, m));
        };
        const enoki::Array<FloatP, 3> v0 = gatherV(0, triIdx, active);
        const enoki::Array<FloatP, 3> v1 = gatherV(1, triIdx, active);
        const enoki::Array<FloatP, 3> v2 = gatherV(2, triIdx, active);
        const enoki::Array<FloatP, 3> n0 = gatherV(3, triIdx, active);
        const enoki::Array<FloatP, 3> n1 = gatherV(4, triIdx, active);
        const enoki::Array<FloatP, 3> n2 = gatherV(5, triIdx, active);

        // Uniform barycentrics.
        const FloatP su = enoki::sqrt(u1);
        const FloatP b0 = FloatP(1.f) - su;
        const FloatP b1 = su * (FloatP(1.f) - u2);
        const FloatP b2 = su * u2;

        const Point3fP lp(fmadd(b0, v0.x(), fmadd(b1, v1.x(), b2 * v2.x())),
                          fmadd(b0, v0.y(), fmadd(b1, v1.y(), b2 * v2.y())),
                          fmadd(b0, v0.z(), fmadd(b1, v1.z(), b2 * v2.z())));

        Normal3fP nl(fmadd(b0, n0.x(), fmadd(b1, n1.x(), b2 * n2.x())),
                     fmadd(b0, n0.y(), fmadd(b1, n1.y(), b2 * n2.y())),
                     fmadd(b0, n0.z(), fmadd(b1, n1.z(), b2 * n2.z())));
        nl = enoki::normalize(nl);

        const Vector3fP delta(lp - p);
        const FloatP distSq = enoki::dot(delta, delta);
        const FloatP dist = enoki::sqrt(distSq);
        const Vector3fP w = delta / dist; // p -> light

        // cos at the light between its normal and the direction back to p.
        const FloatP cosLight = enoki::dot(nl, -w);
        const FloatP cosShading = enoki::dot(n, w);

        const MaskP valid = active && (cosLight > FloatP(0.f)) &&
                            (cosShading > FloatP(0.f)) && (dist > FloatP(EPS_DENOM));

        // pdf = distSq / (totalArea * cosLight).
        const FloatP denom = enoki::select(cosLight > FloatP(EPS_DENOM), cosLight,
                                           FloatP(EPS_DENOM));
        const FloatP pdfVal = distSq / (FloatP(m_totalArea) * denom);

        enoki::masked(*lightP, active) = lp;
        enoki::masked(*wi, active) = w;
        enoki::masked(*lightN, active) = nl;
        // Callers are not guaranteed to zero *pdf.
        enoki::masked(*pdf, active) = enoki::select(valid, pdfVal, FloatP(0.f));

        // Emitted radiance = plain Le * gain.
        DifferentialGeometryP dg;
        dg.p = lp;
        dg.n = nl;
        dg.ng = nl; // Le's sidedness test reads the face normal.
        dg.uv_u = FloatP(0.f);
        dg.uv_v = FloatP(0.f);
        enoki::masked(*LeOut, active) =
            m_Le->Evaluate(dg, sw, active) * FloatP(m_gain);

        return valid;
    }

    FloatP AreaLight::Pdf_L(const Point3fP &p, const Normal3fP &n,
                            const Point3fP &lightP, const Normal3fP &lightN,
                            MaskP active) const
    {
        if (m_triCount == 0)
            return FloatP(0.f);

        // lightP/lightN come from the integrator's intersection with the emitter.
        const Vector3fP delta(lightP - p);
        const FloatP distSq = enoki::dot(delta, delta);
        const FloatP dist = enoki::sqrt(distSq);
        const Vector3fP w = delta / dist; // p -> light

        const FloatP cosLight = enoki::dot(lightN, -w);
        const FloatP cosShading = enoki::dot(n, w);
        const MaskP valid = active && (cosLight > FloatP(0.f)) &&
                            (cosShading > FloatP(0.f)) && (dist > FloatP(EPS_DENOM));

        const FloatP denom = enoki::select(cosLight > FloatP(EPS_DENOM), cosLight,
                                           FloatP(EPS_DENOM));
        const FloatP pdfVal = distSq / (FloatP(m_totalArea) * denom);
        return enoki::select(valid, pdfVal, FloatP(0.f));
    }

    std::shared_ptr<Light> AreaLight::CreateLight(const PluginContext &ctx)
    {
        // Emission color/texture.
        std::shared_ptr<ColorTexture> le;
        const std::string leName = ctx.params ? ctx.params->FindTexture("L") : "";
        if (!leName.empty() && ctx.colorTextures)
        {
            auto it = ctx.colorTextures->find(leName);
            if (it != ctx.colorTextures->end())
                le = it->second;
        }
        if (!le)
        {
            const RGBColor rgb =
                ctx.params ? ctx.params->FindOneRGBColor("L", RGBColor(1.f))
                           : RGBColor(1.f);
            const RGBColorP rgbP(FloatP(rgb.r()), FloatP(rgb.g()), FloatP(rgb.b()));
            le = std::make_shared<ConstantColorTexture>(rgbP, /*illuminant=*/true);
        }

        const float gain = ctx.params ? ctx.params->FindOneFloat("gain", 1.f) : 1.f;
        return std::make_shared<AreaLight>(std::move(le), gain);
    }

    LUX2_REGISTER_LIGHT(AreaLight, "area");

} // namespace lux2
