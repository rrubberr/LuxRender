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

#include "materials/glass.h"

#include "core/bsdf_util.h"
#include "core/dynload.h"
#include "core/material_util.h"
#include "core/math.h"
#include "core/paramset.h"
#include "core/register.h"

#include <enoki/array.h>

namespace lux2
{

    namespace
    {
        // Cauchy IOR eta(lambda) = index + cb / lambda^2.
        inline FloatP cauchyIor(const FloatP &index, const FloatP &cb,
                                const FloatP &lambdaNm)
        {
            return index + cb / (lambdaNm * lambdaNm);
        }

        // Thin film of the reflection Fresnel where
        // f *= cos^2(4*pi*film/lambda * sqrt(filmindex^2 - sin^2 theta) + pi).
        inline FloatP filmPhase(const FloatP &film, const FloatP &filmindex,
                                const FloatP &sinTheta2, const FloatP &lambdaNm)
        {
            const FloatP s =
                sqrt(max(filmindex * filmindex - sinTheta2, FloatP(0.f)));
            const FloatP pd =
                fmadd(FloatP(4.f) * PI * film / lambdaNm, s, FloatP(PI));
            const FloatP c = cos(pd);
            return c * c;
        }
    } // namespace

    FloatP GlassMaterial::FresnelAt(const SpectrumWavelengthsP &sw,
                                    const DifferentialGeometryP &dg,
                                    const FloatP &cosi, MaskP active) const
    {
        FresnelGeneralP fg;
        fg.eta = cauchyIor(m_index->Evaluate(dg, sw, active),
                           m_cauchyb->Evaluate(dg, sw, active), sw.w);
        fg.k = FloatP(0.f);
        fg.model = FresnelModel::Dielectric;
        // Reproduces Lux FresnelCauchy two sided behavior
        // where F = 1 under TIR.
        return FresnelGeneralEvaluate(fg, cosi);
    }

    void GlassMaterial::SampleF(const SpectrumWavelengthsP &sw,
                                const Vector3fP &wo,
                                const DifferentialGeometryP &dg,
                                const FloatP &, const FloatP &,
                                const FloatP &ucomp, BSDFSampleP *sample,
                                TransportMode mode, MaskP active) const
    {
        if (none(active))
            return;

        // Lux evaluates (sn, tn, dgShading.nn) with signed cosines
        // so worldspace dot products against the unflipped shading
        // normal are equivalent.
        const FloatP cosi = dot(wo, dg.n);
        const MaskP entering = cosi > FloatP(0.f);

        const FloatP R = min(m_Kr->Evaluate(dg, sw, active), FloatP(1.f));
        const FloatP T = min(m_Kt->Evaluate(dg, sw, active), FloatP(1.f));

        const FloatP F = FresnelAt(sw, dg, cosi, active);

        // Lux MultiBSDF component weights are pure Fresnel without Kr/Kt.
        // A component exists where K is not black.
        FloatP wR, wT;
        if (m_architectural)
        {
            // ArchitecturalReflection::Weight takes F on the front side only.
            wR = select(cosi > FloatP(0.f), F, FloatP(0.f));
            // SimpleSpecularTransmission::Weight needs passthrough from
            // back, double-bounce attenuation from front.
            const FloatP one(1.f);
            const FloatP Fp = F * fmadd(one - F, one - F, one);
            wT = select(cosi < FloatP(0.f), FloatP(1.f), one - Fp);
        }
        else
        {
            wR = F;
            wT = FloatP(1.f) - F;
        }
        wR = select(R > FloatP(0.f), wR, FloatP(0.f));
        wT = select(T > FloatP(0.f), wT, FloatP(0.f));

        const FloatP total = wR + wT;
        const MaskP selOk = total > FloatP(0.f);

        // Lux selection u3 *= totalWeight, scan components in order,
        // reflection added by Glass::GetBSDF. Lux falls back to the
        // last positive weight component when rounding leaves u3 >= 0.
        const MaskP pickR =
            selOk && (((ucomp * total) < wR) || !(wT > FloatP(0.f)));
        const FloatP w = select(pickR, wR, wT) / max(total, EPS_DENOM);

        const FloatP sini2 = max(FloatP(1.f) - cosi * cosi, FloatP(0.f));
        const FloatP ior = cauchyIor(m_index->Evaluate(dg, sw, active),
                                     m_cauchyb->Evaluate(dg, sw, active), sw.w);

        // Reflection wi = 2(n . wo)n - wo matching local (-x, -y, z).
        const Vector3fP n(dg.n.x(), dg.n.y(), dg.n.z());
        const Vector3fP wiR = n * (FloatP(2.f) * cosi) - wo;

        FloatP fR = F * R;
        const FloatP film = m_film->Evaluate(dg, sw, active);
        const FloatP filmindex = m_filmindex->Evaluate(dg, sw, active);
        const MaskP hasFilm = film > FloatP(0.f);
        if (any(hasFilm))
        {
            const FloatP ph = filmPhase(film, filmindex, sini2, sw.w);
            fR = select(hasFilm, F * R * ph, F * R);
        }

        // Transmission Snell with eta = 1/ior entering or architectural,
        // ior exiting wi = -eta*wo_t + cos(t)*n.
        const FloatP eta =
            select(entering || MaskP(m_architectural), FloatP(1.f) / ior, ior);
        const FloatP sint2 = eta * eta * sini2;
        // Lux fails the sample under TIR; with F = 1 exactly on TIR lanes
        // wT is 0 so they are never selected.
        const MaskP tir = sint2 >= FloatP(1.f);
        const FloatP cost = sqrt(max(FloatP(1.f) - sint2, FloatP(0.f)));
        const FloatP cosT = select(entering, -cost, cost);
        Vector3fP wiT = fmadd(n, fmadd(eta, cosi, cosT), wo * (-eta));
        if (m_architectural)
            wiT = -wo; // straight through without refraction

        FloatP fT;
        if (m_architectural)
        {
            // Lux architectural glass where reverse=true (Radiance) applies
            // the entry Fresnel on the exit side and reverse=false
            // (Importance) on the entry side.
            const FloatP one(1.f);
            const FloatP Fe = mode == TransportMode::Radiance
                                  ? select(entering, FloatP(0.f),
                                           FresnelAt(sw, dg, -cosi, active))
                                  : select(entering, F, FloatP(0.f));
            fT = T * (one - Fe * fmadd(one - Fe, one - Fe, one));
        }
        else if (mode == TransportMode::Radiance)
        {
            // Lux reverse=true where (1 - F(cost)) * eta^2 * T. Fresnel is
            // evaluated at the signed transmitted cosine and eta^2 is the
            // radiance Jacobian on the eye path.
            const FloatP Ft = FresnelAt(sw, dg, cosT, active);
            fT = (FloatP(1.f) - Ft) * (eta * eta) * T;
        }
        else
        {
            // Lux reverse=false: (1 - F(cosi)) * |cosi / cost| * T,
            // the cosine ratio is the light path Jacobian.
            fT = (FloatP(1.f) - F) * abs(cosi / max(cost, EPS_DENOM)) * T;
        }

        const Vector3fP wi = select(pickR, wiR, wiT);
        // Lux MultiBSDF specular branch pdf *= w and f /= w. The 1/w
        // gain cancels the Fresnel weight (f_R = F*R/F = R when both lobes
        // exist) which keeps prevPdf = w correct for FULL_MIS.
        FloatP f = select(pickR, fR, fT) / max(w, EPS_DENOM);
        FloatP pdf = w;

        // Lux side test against the geometric normal with a grazing
        // epsilon where st > 0 rejects BTDFs, st < 0 rejects BRDFs,
        // st == 0 rejects everything. TIR only kills the transmission
        // lobe.
        const FloatP st = sideTest(wo, wi, dg);
        const MaskP lobeOk = select(pickR, st > FloatP(0.f),
                                    (st < FloatP(0.f)) && !tir);

        const MaskP ok = active && selOk && lobeOk && (w > FloatP(0.f));
        // Lux applies the ng Jacobian only when reverse=false
        // (Importance); the eye path (Radiance) skips it.
        f = select(ok, f * modeJacobian(mode, st), SWCSpectrumP(0.f));
        pdf = select(ok, pdf, FloatP(0.f));

        enoki::masked(sample->wi, active) = wi;
        enoki::masked(sample->f, active) = f;
        enoki::masked(sample->pdf, active) = pdf;
        enoki::masked(sample->eta, active) =
            select(entering, FloatP(1.f) / ior, ior);
        enoki::masked(sample->sampledType, active) =
            select(pickR, UInt32P(uint32_t(BSDFType::SpecularReflection)),
                   UInt32P(uint32_t(BSDFType::SpecularTransmission)));
        enoki::masked(sample->specular, active) = MaskP(true);
    }

    void GlassMaterial::Eval(const SpectrumWavelengthsP &sw,
                             const Vector3fP &wi, const Vector3fP &wo,
                             const DifferentialGeometryP &dg, TransportMode mode,
                             BSDFEvalP *out, MaskP active) const
    {
        if (none(active))
            return;

        // lux NEE evaluates MultiBSDF::F(reverse=true) so non-architectural
        // specular lobe F()s are empty and contribute zero NEE. Radiance
        // (reverse=true) applies no ng Jacobian; Importance (reverse=false)
        // multiplies by |sideTest|.
        FloatP f(0.f);
        if (m_architectural)
        {
            // SimpleSpecularTransmission::F is straight through only,
            // side test transmission lobe requires wi on the opposite
            // side of ng from wo.
            const FloatP cosi = dot(wo, dg.n);
            const MaskP entering = cosi > FloatP(0.f);
            const FloatP sini2 = max(FloatP(1.f) - cosi * cosi, FloatP(0.f));
            const FloatP ior = cauchyIor(m_index->Evaluate(dg, sw, active),
                                         m_cauchyb->Evaluate(dg, sw, active),
                                         sw.w);
            const FloatP eta = FloatP(1.f) / ior;
            const MaskP tir = (eta * eta * sini2) >= FloatP(1.f);

            const FloatP F = FresnelAt(sw, dg, cosi, active);
            const FloatP Fe = select(entering, F, FloatP(0.f));
            const FloatP one(1.f);
            const SWCSpectrumP T =
                min(m_Kt->Evaluate(dg, sw, active), FloatP(1.f));

            const FloatP st = sideTest(wo, wi, dg);
            const MaskP straight =
                dot(wo, wi) <= (FloatP(-1.f) + EPS_DENOM);
            const MaskP ok = active && straight && !tir && (st < FloatP(0.f));
            f = select(ok,
                       T * (one - Fe * fmadd(one - Fe, one - Fe, one)),
                       SWCSpectrumP(0.f));
            // Legacy SingleBSDF::F applies |sideTest| only when reverse=false.
            f = f * modeJacobian(mode, st);
        }

        enoki::masked(out->f, active) = f;
        enoki::masked(out->pdf, active) = FloatP(0.f);
        enoki::masked(out->pdfRev, active) = FloatP(0.f);
    }

    std::shared_ptr<Material> GlassMaterial::CreateMaterial(
        const PluginContext &ctx)
    {
        auto Kr = getColorTex(ctx, "Kr", RGBColor(1.f));
        auto Kt = getColorTex(ctx, "Kt", RGBColor(1.f));
        auto index = getFloatTex(ctx, "index", 1.5f);
        auto cauchyb = getFloatTex(ctx, "cauchyb", 0.f);
        auto film = getFloatTex(ctx, "film", 0.f);
        auto filmindex = getFloatTex(ctx, "filmindex", 1.5f);
        const bool archi =
            ctx.params ? ctx.params->FindOneBool("architectural", false) : false;

        return std::make_shared<GlassMaterial>(
            std::move(Kr), std::move(Kt), std::move(index),
            std::move(cauchyb), std::move(film), std::move(filmindex), archi);
    }

    LUX2_REGISTER_MATERIAL(GlassMaterial, "glass");

} // namespace lux2
