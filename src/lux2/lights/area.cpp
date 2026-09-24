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
        m_v0x.resize(m_triCount);
        m_v0y.resize(m_triCount);
        m_v0z.resize(m_triCount);
        m_v1x.resize(m_triCount);
        m_v1y.resize(m_triCount);
        m_v1z.resize(m_triCount);
        m_v2x.resize(m_triCount);
        m_v2y.resize(m_triCount);
        m_v2z.resize(m_triCount);
        m_n0x.resize(m_triCount);
        m_n0y.resize(m_triCount);
        m_n0z.resize(m_triCount);
        m_n1x.resize(m_triCount);
        m_n1y.resize(m_triCount);
        m_n1z.resize(m_triCount);
        m_n2x.resize(m_triCount);
        m_n2y.resize(m_triCount);
        m_n2z.resize(m_triCount);
        m_cdf.resize(m_triCount);

        m_totalArea = 0.f;
        for (std::uint32_t i = 0; i < m_triCount; ++i)
        {
            const TriangleDesc &t = tris[i];
            m_v0x[i] = t.v0.x();
            m_v0y[i] = t.v0.y();
            m_v0z[i] = t.v0.z();
            m_v1x[i] = t.v1.x();
            m_v1y[i] = t.v1.y();
            m_v1z[i] = t.v1.z();
            m_v2x[i] = t.v2.x();
            m_v2y[i] = t.v2.y();
            m_v2z[i] = t.v2.z();
            m_n0x[i] = t.n0.x();
            m_n0y[i] = t.n0.y();
            m_n0z[i] = t.n0.z();
            m_n1x[i] = t.n1.x();
            m_n1y[i] = t.n1.y();
            m_n1z[i] = t.n1.z();
            m_n2x[i] = t.n2.x();
            m_n2y[i] = t.n2.y();
            m_n2z[i] = t.n2.z();

            // Triangle area = 0.5 * |(v1-v0) x (v2-v0)|.
            const Vector3f e1 = t.v1 - t.v0;
            const Vector3f e2 = t.v2 - t.v0;
            const Vector3f cr = enoki::cross(e1, e2);
            const float area = 0.5f * std::sqrt(enoki::dot(cr, cr));
            m_totalArea += area;
            m_cdf[i] = m_totalArea;
        }
    }

    SWCSpectrumP AreaLight::Le(const RayP &ray, MaskP active) const
    {
        DifferentialGeometryP dg;
        dg.p = ray(ray.maxt);
        dg.n = Normal3fP(FloatP(0.f), FloatP(0.f), FloatP(1.f));
        dg.uv_u = FloatP(0.f);
        dg.uv_v = FloatP(0.f);

        // Expand to a full sw so the color texture can reconstruct its spectrum.
        SpectrumWavelengthsP sw;
        sw.FromWavelength(ray.wavelengths);
        return m_Le->Evaluate(dg, sw, active) * FloatP(m_gain * PI);
    }

    MaskP AreaLight::Sample_L(const SpectrumWavelengthsP &sw,
                              const Point3fP &p, const Normal3fP &n,
                              const FloatP &u0, const FloatP &u1, const FloatP &u2,
                              Point3fP *lightP, Vector3fP *wi,
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
        const FloatP v0x = enoki::gather<FloatP>(m_v0x.data(), triIdx, active);
        const FloatP v0y = enoki::gather<FloatP>(m_v0y.data(), triIdx, active);
        const FloatP v0z = enoki::gather<FloatP>(m_v0z.data(), triIdx, active);
        const FloatP v1x = enoki::gather<FloatP>(m_v1x.data(), triIdx, active);
        const FloatP v1y = enoki::gather<FloatP>(m_v1y.data(), triIdx, active);
        const FloatP v1z = enoki::gather<FloatP>(m_v1z.data(), triIdx, active);
        const FloatP v2x = enoki::gather<FloatP>(m_v2x.data(), triIdx, active);
        const FloatP v2y = enoki::gather<FloatP>(m_v2y.data(), triIdx, active);
        const FloatP v2z = enoki::gather<FloatP>(m_v2z.data(), triIdx, active);
        const FloatP n0x = enoki::gather<FloatP>(m_n0x.data(), triIdx, active);
        const FloatP n0y = enoki::gather<FloatP>(m_n0y.data(), triIdx, active);
        const FloatP n0z = enoki::gather<FloatP>(m_n0z.data(), triIdx, active);
        const FloatP n1x = enoki::gather<FloatP>(m_n1x.data(), triIdx, active);
        const FloatP n1y = enoki::gather<FloatP>(m_n1y.data(), triIdx, active);
        const FloatP n1z = enoki::gather<FloatP>(m_n1z.data(), triIdx, active);
        const FloatP n2x = enoki::gather<FloatP>(m_n2x.data(), triIdx, active);
        const FloatP n2y = enoki::gather<FloatP>(m_n2y.data(), triIdx, active);
        const FloatP n2z = enoki::gather<FloatP>(m_n2z.data(), triIdx, active);

        // Uniform barycentrics.
        const FloatP su = enoki::sqrt(u1);
        const FloatP b0 = FloatP(1.f) - su;
        const FloatP b1 = su * (FloatP(1.f) - u2);
        const FloatP b2 = su * u2;

        const Point3fP lp(fmadd(b0, v0x, fmadd(b1, v1x, b2 * v2x)),
                          fmadd(b0, v0y, fmadd(b1, v1y, b2 * v2y)),
                          fmadd(b0, v0z, fmadd(b1, v1z, b2 * v2z)));

        Normal3fP nl(fmadd(b0, n0x, fmadd(b1, n1x, b2 * n2x)),
                     fmadd(b0, n0y, fmadd(b1, n1y, b2 * n2y)),
                     fmadd(b0, n0z, fmadd(b1, n1z, b2 * n2z)));
        nl = enoki::normalize(nl);

        const Vector3fP delta(lp - p);
        const FloatP distSq = enoki::dot(delta, delta);
        const FloatP dist = enoki::sqrt(distSq);
        const Vector3fP w = delta / dist; // p -> light

        // cos at the light between its normal and the direction back to p.
        const FloatP cosLight = enoki::dot(nl, -w);
        const FloatP cosShading = enoki::dot(n, w);

        const MaskP valid = active && (cosLight > FloatP(0.f)) &&
                            (cosShading > FloatP(0.f)) && (dist > FloatP(1e-8f));

        // pdf = distSq / (totalArea * cosLight).
        const FloatP denom = enoki::select(cosLight > FloatP(1e-8f), cosLight,
                                           FloatP(1e-8f));
        const FloatP pdfVal = distSq / (FloatP(m_totalArea) * denom);

        enoki::masked(*lightP, active) = lp;
        enoki::masked(*wi, active) = w;
        enoki::masked(*pdf, active) = enoki::select(valid, pdfVal, FloatP(0.f));

        // Emitted radiance: Le * gain * PI.
        DifferentialGeometryP dg;
        dg.p = lp;
        dg.n = nl;
        dg.uv_u = FloatP(0.f);
        dg.uv_v = FloatP(0.f);
        enoki::masked(*LeOut, active) =
            m_Le->Evaluate(dg, sw, active) * FloatP(m_gain * PI);

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
                            (cosShading > FloatP(0.f)) && (dist > FloatP(1e-8f));

        const FloatP denom = enoki::select(cosLight > FloatP(1e-8f), cosLight,
                                           FloatP(1e-8f));
        const FloatP pdfVal = distSq / (FloatP(m_totalArea) * denom);
        return enoki::select(valid, pdfVal, FloatP(0.f));
    }

    std::shared_ptr<Light> AreaLight::CreateLight(const PluginContext &ctx)
    {
        // Emission color/texture.
        std::shared_ptr<ColorTexture> le;
        const std::string leName = ctx.params ? ctx.params->FindTexture("Le") : "";
        if (!leName.empty() && ctx.colorTextures)
        {
            auto it = ctx.colorTextures->find(leName);
            if (it != ctx.colorTextures->end())
                le = it->second;
        }
        if (!le)
        {
            const RGBColor rgb =
                ctx.params ? ctx.params->FindOneRGBColor("Le", RGBColor(1.f))
                           : RGBColor(1.f);
            const RGBColorP rgbP(FloatP(rgb.r()), FloatP(rgb.g()), FloatP(rgb.b()));
            le = std::make_shared<ConstantColorTexture>(rgbP);
        }

        const float gain = ctx.params ? ctx.params->FindOneFloat("gain", 1.f) : 1.f;
        return std::make_shared<AreaLight>(std::move(le), gain);
    }

    LUX2_REGISTER_LIGHT(AreaLight, "area");

} // namespace lux2
