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

#include "materials/matte.h"

#include "core/bsdf_util.h"
#include "core/dynload.h"
#include "core/material_util.h"
#include "core/math.h"
#include "core/register.h"
#include "core/sampling.h"

#include <enoki/array.h>

namespace lux2
{

    void MatteMaterial::coefficients(const DifferentialGeometryP &dg,
                                     const SpectrumWavelengthsP &sw,
                                     MaskP active, SWCSpectrumP *R,
                                     FloatP *A, FloatP *B) const
    {
        // lordcrc's clamp where Kd is [0,1] to avoid >1 reflection.
        // Thanks lordcrc!
        *R = min(max(m_Kd->Evaluate(dg, sw, active), FloatP(0.f)),
                 FloatP(1.f));

        // Lux OrenNayar ctor with sigma clamped to [0,90] degrees.
        const FloatP sig = min(max(m_sigma->Evaluate(dg, sw, active),
                                   FloatP(0.f)),
                               FloatP(90.f));
        const FloatP s = sig * (PI / FloatP(180.f));
        const FloatP sigma2 = s * s;
        *A = FloatP(1.f) - sigma2 / (FloatP(2.f) * (sigma2 + FloatP(0.33f)));
        *B = FloatP(0.45f) * sigma2 / (sigma2 + FloatP(0.09f));
    }

    void MatteMaterial::SampleF(const SpectrumWavelengthsP &sw,
                                const Vector3fP &wo,
                                const DifferentialGeometryP &dg,
                                const FloatP &u0, const FloatP &u1,
                                const FloatP &, BSDFSampleP *sample,
                                TransportMode mode, MaskP active) const
    {
        if (none(active))
            return;

        SWCSpectrumP R;
        FloatP A, B;
        coefficients(dg, sw, active, &R, &A, &B);

        // Lux Lambertian/OrenNayar SampleF to cosine sample the hemisphere
        // about the unflipped shading normal and flip with wo's side.
        const Vector3fP wi = cosineSampleHemisphere(dg.n, wo, u0, u1);
        const MaskP sameHemi = sameHemisphere(wo, wi, dg.n);

        const FloatP cosO = dot(wo, dg.n);
        const FloatP cosI = dot(wi, dg.n);

        // Oren-Nayar term that reduces to Lambertian at sigma == 0.
        const FloatP sinI2 = max(FloatP(1.f) - cosI * cosI, FloatP(0.f));
        const FloatP sinO2 = max(FloatP(1.f) - cosO * cosO, FloatP(0.f));
        const FloatP sinI = sqrt(sinI2);
        const FloatP sinO = sqrt(sinO2);

        // maxcos = cos(phi_i - phi_o), cosine of the angle between the
        // tangent plane projections. Lux uses sintheta > 1e-4 for
        // both directions.
        const Vector3fP n(dg.n.x(), dg.n.y(), dg.n.z());
        const Vector3fP wiT = wi - n * cosI;
        const Vector3fP woT = wo - n * cosO;
        const FloatP dcos =
            dot(wiT, woT) /
            max(sqrt(max(dot(wiT, wiT), FloatP(0.f))) *
                    sqrt(max(dot(woT, woT), FloatP(0.f))),
                EPS_DENOM);
        const MaskP phOk = (sinI > FloatP(1e-4f)) && (sinO > FloatP(1e-4f));
        const FloatP maxcos = select(phOk, max(dcos, FloatP(0.f)),
                                     FloatP(0.f));

        const FloatP on =
            A + B * maxcos * sinO * sinI /
                    max(max(abs(cosI), abs(cosO)), EPS_DENOM);

        // Eye path (Radiance is Lux reverse=true): f = ON * R, the
        // cosine/pdf factors cancel. Importance folds |cos_o / cos_i|
        // (Lux SampleF) then the ng Jacobian (SingleBSDF).
        FloatP f = on * R;
        if (mode == TransportMode::Importance)
            f = f * abs(cosO / max(abs(cosI), EPS_DENOM));

        const FloatP st = sideTest(wo, wi, dg);
        // BRDF: st > 0 required; st < 0 or grazing st == 0 kills the lane.
        const MaskP ok = active && sameHemi && (st > FloatP(0.f));

        const FloatP pdf = abs(cosI) * INVPI;
        f = select(ok, f * modeJacobian(mode, st), SWCSpectrumP(0.f));

        enoki::masked(sample->wi, active) = wi;
        enoki::masked(sample->f, active) = f;
        enoki::masked(sample->pdf, active) = select(ok, pdf, FloatP(0.f));
        enoki::masked(sample->eta, active) = FloatP(1.f);
        enoki::masked(sample->sampledType, active) =
            UInt32P(uint32_t(BSDFType::DiffuseReflection));
        enoki::masked(sample->specular, active) = MaskP(false);
    }

    void MatteMaterial::Eval(const SpectrumWavelengthsP &sw,
                             const Vector3fP &wi, const Vector3fP &wo,
                             const DifferentialGeometryP &dg,
                             TransportMode mode, BSDFEvalP *out,
                             MaskP active) const
    {
        if (none(active))
            return;

        // Lux NEE calls SingleBSDF::F(sw, wi_light, wo_eye, reverse=true) where
        // Lambertian/OrenNayar F() is non-empty so matte contributes to NEE.
        // The integrator divides by the light pdf only.
        SWCSpectrumP R;
        FloatP A, B;
        coefficients(dg, sw, active, &R, &A, &B);

        const FloatP cosL = abs(dot(wi, dg.n));
        const FloatP cosE = abs(dot(wo, dg.n));
        const FloatP sinL = sqrt(max(FloatP(1.f) - cosL * cosL, FloatP(0.f)));
        const FloatP sinE = sqrt(max(FloatP(1.f) - cosE * cosE, FloatP(0.f)));

        const Vector3fP n(dg.n.x(), dg.n.y(), dg.n.z());
        const Vector3fP wiT = wi - n * dot(wi, dg.n);
        const Vector3fP woT = wo - n * dot(wo, dg.n);
        const FloatP dcos =
            dot(wiT, woT) /
            max(sqrt(max(dot(wiT, wiT), FloatP(0.f))) *
                    sqrt(max(dot(woT, woT), FloatP(0.f))),
                EPS_DENOM);
        const MaskP phOk = (sinL > FloatP(1e-4f)) && (sinE > FloatP(1e-4f));
        const FloatP maxcos =
            select(phOk, max(dcos, FloatP(0.f)), FloatP(0.f));

        // Lux F(wo=light, wi=eye): INVPI * |cos(light)| *
        // (A + B * maxcos * sinL * sinE / max(|cos|,|cos|)) * R.
        const FloatP on =
            A + B * maxcos * sinL * sinE /
                    max(max(cosL, cosE), EPS_DENOM);
        SWCSpectrumP f = INVPI * cosL * on * R;

        // Lux side test in F: Dot(wo_eye, ng) / Dot(wi_light, ng);
        // st < 0 rejects the BRDF, st == 0 rejects everything.
        const FloatP st = sideTest(wi, wo, dg);
        const MaskP ok = active && (st > FloatP(0.f));
        f = select(ok, f * modeJacobian(mode, st), SWCSpectrumP(0.f));

        enoki::masked(out->f, active) = f;
        // Solid angle densities for MIS (Lux BxDF::Pdf both ways).
        const MaskP sameHemi = sameHemisphere(wo, wi, dg.n);
        enoki::masked(out->pdf, active) =
            select(sameHemi, abs(dot(wi, dg.n)) * INVPI, FloatP(0.f));
        enoki::masked(out->pdfRev, active) =
            select(sameHemi, abs(dot(wo, dg.n)) * INVPI, FloatP(0.f));
    }

    std::shared_ptr<Material> MatteMaterial::CreateMaterial(
        const PluginContext &ctx)
    {
        auto Kd = getColorTex(ctx, "Kd", RGBColor(0.9f));
        auto sigma = getFloatTex(ctx, "sigma", 0.f);
        return std::make_shared<MatteMaterial>(std::move(Kd),
                                               std::move(sigma));
    }

    LUX2_REGISTER_MATERIAL(MatteMaterial, "matte");

} // namespace lux2
