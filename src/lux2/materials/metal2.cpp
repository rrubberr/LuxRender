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

#include "core/dynload.h"
#include "core/fresnel.h"
#include "core/math.h"
#include "core/paramset.h"
#include "core/register.h"
#include "core/schlick_distribution.h"
#include "textures/constant.h"

namespace lux2
{

    namespace
    {

        // Local shading frame: z == n, x == tangent (dp_ds), y == cross(n, x).
        struct Frame
        {
            Vector3fP x, y, z;
        };

        Frame MakeFrame(const DifferentialGeometryP &dg)
        {
            Frame f;
            f.z = Vector3fP(dg.n);
            f.x = enoki::normalize(dg.dp_ds);
            f.y = enoki::normalize(cross(f.z, f.x));
            return f;
        }

        Vector3fP ToLocal(const Frame &fr, const Vector3fP &v)
        {
            return Vector3fP(dot(v, fr.x), dot(v, fr.y), dot(v, fr.z));
        }

        Vector3fP ToWorld(const Frame &fr, const Vector3fP &v)
        {
            return v.x() * fr.x + v.y() * fr.y + v.z() * fr.z;
        }

        // u*v roughness and the anisotropy term from squared roughnesses.
        void DistrParams(const FloatP &u, const FloatP &v,
                         FloatP *roughness, FloatP *anisotropy)
        {
            const FloatP u2 = u * u;
            const FloatP v2 = v * v;
            *roughness = u * v;
            // anisotropy = u2 < v2 ? 1 - u2/v2 : v2/u2 - 1.
            const FloatP u2s = select(u2 > FloatP(0.f), u2, FloatP(1.f));
            const FloatP v2s = select(v2 > FloatP(0.f), v2, FloatP(1.f));
            const FloatP aLt = FloatP(1.f) - u2 / v2s;
            const FloatP aGe = v2 / u2s - FloatP(1.f);
            *anisotropy = select(u2 < v2, aLt, aGe);
        }

    } // namespace

    void Metal2Material::SampleF(const SpectrumWavelengthsP &sw, const Vector3fP &wo,
                                 const DifferentialGeometryP &dg, const FloatP &u0,
                                 const FloatP &u1, const FloatP &, BSDFSampleP *s,
                                 TransportMode, MaskP active) const
    {
        const Frame fr = MakeFrame(dg);
        const Vector3fP woL = ToLocal(fr, wo);

        const FloatP u = m_nu->Evaluate(dg, sw, active);
        const FloatP v = m_nv->Evaluate(dg, sw, active);
        FloatP roughness, anisotropy;
        DistrParams(u, v, &roughness, &anisotropy);
        const SchlickDistribution distr(roughness, anisotropy);

        Vector3fP whL;
        FloatP d, pdfH;
        distr.SampleH(u0, u1, &whL, &d, &pdfH);
        // Reflect wo about the upper half-vector.
        whL = select(whL.z() < FloatP(0.f), -whL, whL);
        const Vector3fP wiL = FloatP(2.f) * dot(woL, whL) * whL - woL;

        const FloatP cosThetaH = dot(woL, whL);
        const FloatP absWiZ = abs(wiL.z());
        const FloatP absWiZSafe = select(absWiZ > FloatP(1e-8f), absWiZ, FloatP(1e-8f));

        const FresnelGeneralP fg = m_fr->Evaluate(dg, sw, active);
        const FloatP F = FresnelGeneralEvaluate(fg, abs(cosThetaH));

        const FloatP G = distr.G(woL, wiL, whL);
        const FloatP factor = d * abs(cosThetaH) / pdfH * G;
        // f excludes the geometric |cos| (integrator applies it): divide by |wi.z|.
        const SWCSpectrumP f = (factor / absWiZSafe) * F;

        const FloatP pdf = pdfH / (FloatP(4.f) * abs(cosThetaH));

        const Vector3fP wi = ToWorld(fr, wiL);

        enoki::masked(s->wo, active) = wi;
        enoki::masked(s->pdf, active) = pdf;
        enoki::masked(s->f, active) = f;
        enoki::masked(s->sampledType, active) = UInt32P(uint32_t(BSDFType::GlossyReflection));
        enoki::masked(s->specular, active) = MaskP(false);
    }

    FloatP Metal2Material::Pdf(const SpectrumWavelengthsP &sw, const Vector3fP &wi,
                               const Vector3fP &wo, const DifferentialGeometryP &dg,
                               uint32_t, TransportMode, MaskP active) const
    {
        const Frame fr = MakeFrame(dg);
        const Vector3fP woL = ToLocal(fr, wo);
        const Vector3fP wiL = ToLocal(fr, wi);

        const FloatP u = m_nu->Evaluate(dg, sw, active);
        const FloatP v = m_nv->Evaluate(dg, sw, active);
        FloatP roughness, anisotropy;
        DistrParams(u, v, &roughness, &anisotropy);
        const SchlickDistribution distr(roughness, anisotropy);

        Vector3fP whL = wiL + woL;
        const FloatP whLen2 = sqr(whL.x()) + sqr(whL.y()) + sqr(whL.z());
        whL = enoki::normalize(whL);
        whL = select(whL.z() < FloatP(0.f), -whL, whL);

        const FloatP denom = FloatP(4.f) * abs(dot(woL, whL));
        const FloatP denomSafe = select(denom > FloatP(1e-8f), denom, FloatP(1e-8f));
        const FloatP pdf = distr.Pdf(whL) / denomSafe;
        return select(active && (whLen2 > FloatP(0.f)), pdf, FloatP(0.f));
    }

    void Metal2Material::Eval(const SpectrumWavelengthsP &sw, const Vector3fP &wi,
                              const Vector3fP &wo, const DifferentialGeometryP &dg,
                              TransportMode, BSDFEvalP *out, MaskP active) const
    {
        const Frame fr = MakeFrame(dg);
        const Vector3fP woL = ToLocal(fr, wo);
        const Vector3fP wiL = ToLocal(fr, wi);

        const FloatP u = m_nu->Evaluate(dg, sw, active);
        const FloatP v = m_nv->Evaluate(dg, sw, active);
        FloatP roughness, anisotropy;
        DistrParams(u, v, &roughness, &anisotropy);
        const SchlickDistribution distr(roughness, anisotropy);

        Vector3fP whL = wiL + woL;
        const FloatP whLen2 = sqr(whL.x()) + sqr(whL.y()) + sqr(whL.z());
        whL = enoki::normalize(whL);
        whL = select(whL.z() < FloatP(0.f), -whL, whL);

        const FloatP cosThetaH = dot(woL, whL);
        const FloatP absWiZ = abs(wiL.z());
        const FloatP absWiZSafe = select(absWiZ > FloatP(1e-8f), absWiZ, FloatP(1e-8f));

        const FresnelGeneralP fg = m_fr->Evaluate(dg, sw, active);
        const FloatP F = FresnelGeneralEvaluate(fg, abs(cosThetaH));

        // d == D(wh) (== pdfH in SampleH), so factor = d*|cosH|/pdfH*G = |cosH|*G.
        const FloatP d = distr.D(whL);
        const FloatP G = distr.G(woL, wiL, whL);
        const FloatP factor = d * abs(cosThetaH) / distr.Pdf(whL) * G;

        // Forward eye-walk pdf pdf(wi|wo) == Pdf(wi,wo); reverse pdf(wo|wi).
        const FloatP pdf = Pdf(sw, wi, wo, dg, 0, TransportMode::Radiance, active);
        const FloatP pdfRev = Pdf(sw, wo, wi, dg, 0, TransportMode::Radiance, active);

        // No valid half-vector (wi,wo back-facing) == no contribution.
        const MaskP valid = active && (whLen2 > FloatP(0.f));
        enoki::masked(out->f, valid) = (factor / absWiZSafe) * F;
        enoki::masked(out->pdf, valid) = pdf;
        enoki::masked(out->pdfRev, valid) = pdfRev;
        // Lanes with no valid half-vector contribute nothing.
        const MaskP invalid = active && !(whLen2 > FloatP(0.f));
        enoki::masked(out->f, invalid) = SWCSpectrumP(0.f);
        enoki::masked(out->pdf, invalid) = FloatP(0.f);
        enoki::masked(out->pdfRev, invalid) = FloatP(0.f);
    }

    std::shared_ptr<Material> Metal2Material::CreateMaterial(const PluginContext &ctx)
    {
        std::shared_ptr<FresnelTexture> fr;
        const std::string frName = ctx.params->FindTexture("fresnel");
        if (!frName.empty() && ctx.fresnelTextures)
        {
            auto it = ctx.fresnelTextures->find(frName);
            if (it != ctx.fresnelTextures->end())
                fr = it->second;
        }
        if (!fr)
        {
            fr = std::make_shared<ConstantFresnelTexture>(
                FloatP(ctx.params->FindOneFloat("fresnel", 5.f)));
        }

        auto nu = std::make_shared<ConstantFloatTexture>(
            FloatP(ctx.params->FindOneFloat("uroughness", 0.1f)));
        auto nv = std::make_shared<ConstantFloatTexture>(
            FloatP(ctx.params->FindOneFloat("vroughness", 0.1f)));

        return std::make_shared<Metal2Material>(std::move(fr), std::move(nu), std::move(nv));
    }

    LUX2_REGISTER_MATERIAL(Metal2Material, "metal2");

} // namespace lux2
