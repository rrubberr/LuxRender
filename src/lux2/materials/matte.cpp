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

#include "core/dynload.h"
#include "core/paramset.h"
#include "core/register.h"
#include "core/texture.h"
#include "textures/constant.h"

namespace lux2
{

    // Oren-Nayar evaluation in the local shading frame (z == dg.n).
    // At sigma == 0 reproduces Lambertian (kd/pi).
    static SWCSpectrumP OrenNayarF(const SWCSpectrumP &R, FloatP sigmaRad,
                                   const Vector3fP &woL, const Vector3fP &wiL)
    {
        const FloatP sigma2 = sigmaRad * sigmaRad;
        const FloatP A = FloatP(1.f) - sigma2 / (FloatP(2.f) * (sigma2 + FloatP(0.33f)));
        const FloatP B = FloatP(0.45f) * sigma2 / (sigma2 + FloatP(0.09f));

        const FloatP sinthetai = sqrt(max(sqr(wiL.x()) + sqr(wiL.y()), FloatP(0.f)));
        const FloatP sinthetao = sqrt(max(sqr(woL.x()) + sqr(woL.y()), FloatP(0.f)));

        // cos(phi_i - phi_o) = cosphi_i*cosphi_o + sinphi_i*sinphi_o, guarded
        // against the sintheta -> 0 poles where phi is undefined.
        const MaskP useCos = (sinthetai > FloatP(1e-4f)) && (sinthetao > FloatP(1e-4f));
        const FloatP sinphii = select(useCos, wiL.y() / sinthetai, FloatP(0.f));
        const FloatP cosphii = select(useCos, wiL.x() / sinthetai, FloatP(1.f));
        const FloatP sinphio = select(useCos, woL.y() / sinthetao, FloatP(0.f));
        const FloatP cosphio = select(useCos, woL.x() / sinthetao, FloatP(1.f));
        const FloatP maxcos = max(FloatP(0.f), cosphii * cosphio + sinphii * sinphio);

        const FloatP cosT = max(abs(wiL.z()), abs(woL.z()));
        const FloatP denom = select(cosT > FloatP(1e-6f), cosT, FloatP(1e-6f));

        const FloatP lobe = A + B * maxcos * sinthetao * sinthetai / denom;
        return R * (lobe * INVPI);
    }

    void MatteMaterial::SampleF(const SpectrumWavelengthsP &sw, const Vector3fP &wo,
                                const DifferentialGeometryP &dg, const FloatP &u0,
                                const FloatP &u1, const FloatP &, BSDFSampleP *s,
                                TransportMode, MaskP active) const
    {
        const Vector3fP n(dg.n);
        const auto [tx, ty] = coordinate_system(n);

        // Cosine weighted hemisphere sample about the z axis of the local frame.
        const FloatP phi = FloatP(2.f) * PI * u0;
        const FloatP cosT = sqrt(max(FloatP(0.f), FloatP(1.f) - u1));
        const FloatP sinT = sqrt(max(FloatP(0.f), u1));
        const Vector3fP wi = sinT * cos(phi) * tx + sinT * sin(phi) * ty + cosT * n;

        // Oren-Nayar coefficients from sigma.
        const FloatP sigmaDeg = clamp(m_sigma->Evaluate(dg, sw, active),
                                      FloatP(0.f), FloatP(90.f));
        const FloatP sigmaRad = sigmaDeg * (PI / FloatP(180.f));

        // Express wo and wi in the local frame (z == n).
        const Vector3fP woL(dot(wo, tx), dot(wo, ty), dot(wo, n));
        const Vector3fP wiL(dot(wi, tx), dot(wi, ty), dot(wi, n));

        const SWCSpectrumP kd = Clamped(m_kd->Evaluate(dg, sw, active));
        const SWCSpectrumP f = OrenNayarF(kd, sigmaRad, woL, wiL);

        enoki::masked(s->wo, active) = wi;
        enoki::masked(s->pdf, active) = cosT * INVPI;
        enoki::masked(s->f, active) = f;
        enoki::masked(s->sampledType, active) = UInt32P(uint32_t(BSDFType::DiffuseReflection));
        enoki::masked(s->specular, active) = MaskP(false);
    }

    FloatP MatteMaterial::Pdf(const SpectrumWavelengthsP &, const Vector3fP &wi,
                              const Vector3fP &, const DifferentialGeometryP &dg,
                              uint32_t, TransportMode, MaskP active) const
    {
        const FloatP cosT = dot(wi, Vector3fP(dg.n));
        const FloatP pdf = cosT * INVPI;
        return select(active && (cosT > FloatP(0.f)), pdf, FloatP(0.f));
    }

    void MatteMaterial::Eval(const SpectrumWavelengthsP &sw, const Vector3fP &wi,
                             const Vector3fP &wo, const DifferentialGeometryP &dg,
                             TransportMode, BSDFEvalP *out, MaskP active) const
    {
        const Vector3fP n(dg.n);
        const auto [tx, ty] = coordinate_system(n);

        // Local frame (z == n) for both directions.
        const Vector3fP woL(dot(wo, tx), dot(wo, ty), dot(wo, n));
        const Vector3fP wiL(dot(wi, tx), dot(wi, ty), dot(wi, n));

        const FloatP sigmaDeg = clamp(m_sigma->Evaluate(dg, sw, active),
                                      FloatP(0.f), FloatP(90.f));
        const FloatP sigmaRad = sigmaDeg * (PI / FloatP(180.f));
        const SWCSpectrumP kd = Clamped(m_kd->Evaluate(dg, sw, active));

        // Forward eye-walk pdf pdf(wi|wo) == Pdf(wi,wo); reverse pdf(wo|wi).
        const FloatP pdf = Pdf(sw, wi, wo, dg, 0, TransportMode::Radiance, active);
        const FloatP pdfRev = Pdf(sw, wo, wi, dg, 0, TransportMode::Radiance, active);

        enoki::masked(out->f, active) = OrenNayarF(kd, sigmaRad, woL, wiL);
        enoki::masked(out->pdf, active) = pdf;
        enoki::masked(out->pdfRev, active) = pdfRev;
    }

    std::shared_ptr<Material> MatteMaterial::CreateMaterial(const PluginContext &ctx)
    {
        std::shared_ptr<ColorTexture> kd;
        const std::string kdName = ctx.params->FindTexture("Kd");
        if (!kdName.empty() && ctx.colorTextures)
        {
            auto it = ctx.colorTextures->find(kdName);
            if (it != ctx.colorTextures->end())
                kd = it->second;
        }
        if (!kd)
        {
            const RGBColor &rgb = ctx.params->FindOneRGBColor("Kd", RGBColor(0.9f));
            const RGBColorP rgbP(FloatP(rgb.r()), FloatP(rgb.g()), FloatP(rgb.b()));
            kd = std::make_shared<ConstantColorTexture>(rgbP);
        }

        std::shared_ptr<FloatTexture> sigma;
        const std::string sigmaName = ctx.params->FindTexture("sigma");
        if (!sigmaName.empty() && ctx.floatTextures)
        {
            auto it = ctx.floatTextures->find(sigmaName);
            if (it != ctx.floatTextures->end())
                sigma = it->second;
        }
        if (!sigma)
        {
            sigma = std::make_shared<ConstantFloatTexture>(
                FloatP(ctx.params->FindOneFloat("sigma", 0.f)));
        }

        return std::make_shared<MatteMaterial>(std::move(kd), std::move(sigma));
    }

    LUX2_REGISTER_MATERIAL(MatteMaterial, "matte");

} // namespace lux2
