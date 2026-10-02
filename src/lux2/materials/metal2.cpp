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

#include "materials/metal2.h"

#include "core/bsdf_util.h"
#include "core/dynload.h"
#include "core/fresnel.h"
#include "core/material_util.h"
#include "core/math.h"
#include "core/register.h"
#include "core/schlick_distribution.h"

#include <enoki/array.h>

namespace lux2
{

    namespace
    {
        // Roughness -> Schlick parameters.
        inline void schlickParams(const DifferentialGeometryP &dg,
                                  const SpectrumWavelengthsP &sw, MaskP active,
                                  const std::shared_ptr<FloatTexture> &nu,
                                  const std::shared_ptr<FloatTexture> &nv,
                                  FloatP *roughness, FloatP *anisotropy)
        {
            const FloatP u = nu->Evaluate(dg, sw, active);
            const FloatP v = nv->Evaluate(dg, sw, active);
            const FloatP u2 = u * u;
            const FloatP v2 = v * v;
            *roughness = u * v;
            // anisotropy = u2 < v2 ? 1 - u2/v2 : v2/u2 - 1, always >= 0.
            // Prevent zero division because Lux degenerate u == v == 0
            // would divide by zero.
            const FloatP a =
                select(u2 < v2, FloatP(1.f) - u2 / max(v2, EPS_DENOM),
                       v2 / max(u2, EPS_DENOM) - FloatP(1.f));
            *anisotropy = max(a, FloatP(0.f));
        }

        // Legacy BSDF WorldToLocal/LocalToWorld in (sn, tn, nn).
        struct Frame
        {
            Vector3fP sn, tn, nn;
            Vector3fP toLocal(const Vector3fP &w) const
            {
                return Vector3fP(dot(w, sn), dot(w, tn), dot(w, nn));
            }
            Vector3fP toWorld(const Vector3fP &v) const
            {
                return sn * v.x() + tn * v.y() + nn * v.z();
            }
        };

        inline Frame makeFrame(const DifferentialGeometryP &dg)
        {
            return {Vector3fP(dg.dp_ds.x(), dg.dp_ds.y(), dg.dp_ds.z()),
                    Vector3fP(dg.dp_dt.x(), dg.dp_dt.y(), dg.dp_dt.z()),
                    Vector3fP(dg.n.x(), dg.n.y(), dg.n.z())};
        }
    } // namespace

    void Metal2Material::SampleF(const SpectrumWavelengthsP &sw,
                                 const Vector3fP &wo,
                                 const DifferentialGeometryP &dg,
                                 const FloatP &u0, const FloatP &u1,
                                 const FloatP &, BSDFSampleP *sample,
                                 TransportMode mode, MaskP active) const
    {
        if (!any(active))
            return;

        FloatP roughness, anisotropy;
        schlickParams(dg, sw, active, m_nu, m_nv, &roughness, &anisotropy);
        const SchlickDistribution md(roughness, anisotropy);
        const FresnelGeneralP fg = m_fresnel->Evaluate(dg, sw, active);
        const Frame fr = makeFrame(dg);

        // Lux MicrofacetReflection::SampleF in (sn,tn,nn).
        const Vector3fP woL = fr.toLocal(wo);
        Vector3fP whL;
        FloatP d, pdf;
        md.SampleH(u0, u1, &whL, &d, &pdf);
        enoki::masked(whL, whL.z() < FloatP(0.f)) = -whL;

        const Vector3fP wiL = whL * (FloatP(2.f) * dot(woL, whL)) - woL;
        const MaskP sameHemi =
            woL.z() * wiL.z() > FloatP(0.f); // oneSided == false

        const FloatP cosThetaH = dot(woL, whL);
        const FloatP G = md.G(woL, wiL, whL);
        const FloatP factor =
            d * abs(cosThetaH) / max(pdf, EPS_DENOM) * G;
        const FloatP F = FresnelGeneralEvaluate(fg, cosThetaH);

        // R == 1 (white). reverse=true (Radiance) divides by |wo.z|;
        // reverse=false (Importance) by |wi.z|.
        FloatP f;
        if (mode == TransportMode::Radiance)
            f = factor / max(abs(woL.z()), EPS_DENOM) * F;
        else
            f = factor / max(abs(wiL.z()), EPS_DENOM) * F;

        pdf = pdf / (FloatP(4.f) * max(abs(cosThetaH), EPS_DENOM));

        const Vector3fP wi = fr.toWorld(wiL);
        const FloatP st = sideTest(wo, wi, dg);
        // BRDF st > 0 required; st < 0 or grazing st == 0 kills the lane.
        const MaskP ok = active && sameHemi && (st > FloatP(0.f));
        f = select(ok, f * modeJacobian(mode, st), SWCSpectrumP(0.f));

        enoki::masked(sample->wi, active) = wi;
        enoki::masked(sample->f, active) = f;
        enoki::masked(sample->pdf, active) = select(ok, pdf, FloatP(0.f));
        enoki::masked(sample->eta, active) = FloatP(1.f);
        enoki::masked(sample->sampledType, active) =
            UInt32P(uint32_t(BSDFType::GlossyReflection));
        enoki::masked(sample->specular, active) = MaskP(false);
    }

    FloatP Metal2Material::Pdf(const SpectrumWavelengthsP &sw,
                               const Vector3fP &wi, const Vector3fP &wo,
                               const DifferentialGeometryP &dg, uint32_t,
                               TransportMode, MaskP active) const
    {
        // Legacy MicrofacetReflection::Pdf: D(wh)/(4|dot(wo,wh)|).
        // wh = normalize(wi + wo) flipped to +z.
        FloatP roughness, anisotropy;
        schlickParams(dg, sw, active, m_nu, m_nv, &roughness, &anisotropy);
        const SchlickDistribution md(roughness, anisotropy);
        const Frame fr = makeFrame(dg);

        const Vector3fP woL = fr.toLocal(wo);
        const Vector3fP wiL = fr.toLocal(wi);
        Vector3fP whL = wiL + woL;
        const FloatP len2 = dot(whL, whL);
        const MaskP valid = active && (len2 > FloatP(0.f));
        enoki::masked(whL, valid) = whL * rsqrt(max(len2, EPS_DENOM));
        enoki::masked(whL, whL.z() < FloatP(0.f)) = -whL;

        const FloatP pdf =
            md.Pdf(whL) / (FloatP(4.f) * max(abs(dot(woL, whL)), EPS_DENOM));
        return select(valid, pdf, FloatP(0.f));
    }

    void Metal2Material::Eval(const SpectrumWavelengthsP &sw,
                              const Vector3fP &wi, const Vector3fP &wo,
                              const DifferentialGeometryP &dg,
                              TransportMode mode, BSDFEvalP *out,
                              MaskP active) const
    {
        if (!any(active))
            return;

        // Lux NEE SingleBSDF::F(sw, woW=light, wiW=eye, reverse=true) ->
        // MicrofacetReflection::F(wo=light, wi=eye). For Lux2 the light
        // direction is wi and the eye direction is wo!
        FloatP roughness, anisotropy;
        schlickParams(dg, sw, active, m_nu, m_nv, &roughness, &anisotropy);
        const SchlickDistribution md(roughness, anisotropy);
        const FresnelGeneralP fg = m_fresnel->Evaluate(dg, sw, active);
        const Frame fr = makeFrame(dg);

        const Vector3fP lightL = fr.toLocal(wi);
        const Vector3fP eyeL = fr.toLocal(wo);
        const FloatP cosO = abs(lightL.z()); // |CosTheta(wo=light)|
        const FloatP cosI = abs(eyeL.z());   // |CosTheta(wi=eye)|

        Vector3fP whL = lightL + eyeL;
        const FloatP len2 = dot(whL, whL);
        const MaskP valid = active && (len2 > FloatP(0.f)) &&
                            (cosO > FloatP(0.f)) && (cosI > FloatP(0.f));
        enoki::masked(whL, valid) = whL * rsqrt(max(len2, EPS_DENOM));
        enoki::masked(whL, whL.z() < FloatP(0.f)) = -whL; // oneSided == false

        const FloatP cosThetaH = dot(eyeL, whL);
        const FloatP F = FresnelGeneralEvaluate(fg, cosThetaH);
        // f carries |cos(light)|, D(wh) * G / (4|cos_eye|) * F.
        SWCSpectrumP f = md.D(whL) * md.G(lightL, eyeL, whL) /
                         (FloatP(4.f) * max(cosI, EPS_DENOM)) *
                         F;

        // Lux SingleBSDF::F side test where dot(wiW=eye, ng)/Dot(woW=light, ng)
        // and st < 0 rejects the BRDF, st == 0 rejects everything.
        const FloatP st = sideTest(wi, wo, dg);
        const MaskP ok = valid && (st > FloatP(0.f));
        f = select(ok, f * modeJacobian(mode, st), SWCSpectrumP(0.f));

        enoki::masked(out->f, active) = f;
        // Solid angle densities for MIS, legacy MicrofacetReflection::Pdf where
        // D(wh)/(4|dot(wo,wh)|). wh bisects the pair so |dot(eye,wh)| ==
        // |dot(light,wh)| and pdf == pdfRev for reflection lobes.
        const FloatP fwd =
            md.Pdf(whL) / (FloatP(4.f) * max(abs(dot(eyeL, whL)), EPS_DENOM));
        enoki::masked(out->pdf, active) = select(valid, fwd, FloatP(0.f));
        enoki::masked(out->pdfRev, active) = select(valid, fwd, FloatP(0.f));
    }

    std::shared_ptr<Material> Metal2Material::CreateMaterial(
        const PluginContext &ctx)
    {
        auto fresnel = getFresnelTex(ctx, "fresnel", 5.f);
        auto nu = getFloatTex(ctx, "uroughness", 0.1f);
        auto nv = getFloatTex(ctx, "vroughness", 0.1f);
        return std::make_shared<Metal2Material>(std::move(fresnel),
                                                std::move(nu), std::move(nv));
    }

    LUX2_REGISTER_MATERIAL(Metal2Material, "metal2");

} // namespace lux2
