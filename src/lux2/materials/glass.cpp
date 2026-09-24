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

#include "core/dynload.h"
#include "core/fresnel.h"
#include "core/paramset.h"
#include "core/register.h"
#include "textures/constant.h"

namespace lux2
{

    namespace
    {

        // Dielectric Fresnel reflectance at cos(theta_i) = cosi (signed: >0 entering),
        // interior IOR = index, exterior = air.
        SWCSpectrumP DielectricFresnel(const FloatP &cosi, const FloatP &index)
        {
            const MaskP entering = cosi > FloatP(0.f);
            const FloatP eta = index;
            const FloatP invEta2 = FloatP(1.f) / (eta * eta);
            const FloatP sini2 = max(FloatP(0.f), FloatP(1.f) - cosi * cosi);
            // sint2 = (entering ? 1/eta^2 : eta^2) * sini2
            const FloatP sint2 = select(entering, invEta2 * sini2, eta * eta * sini2);
            const MaskP tir = sint2 >= FloatP(1.f);
            const FloatP cost = sqrt(max(FloatP(0.f), FloatP(1.f) - sint2));
            const SWCSpectrumP etaS = select(entering, SWCSpectrumP(eta),
                                             SWCSpectrumP(FloatP(1.f) / eta));
            const SWCSpectrumP F = FrDiel2(abs(cosi), SWCSpectrumP(cost), etaS);
            return select(tir, SWCSpectrumP(FloatP(1.f)), F);
        }

    } // namespace

    void GlassMaterial::SampleF(const SpectrumWavelengthsP &sw, const Vector3fP &wo,
                                const DifferentialGeometryP &dg, const FloatP &,
                                const FloatP &, const FloatP &u2, BSDFSampleP *s,
                                TransportMode, MaskP active) const
    {
        const Vector3fP n(dg.n);
        const auto [tx, ty] = coordinate_system(n);

        // Local frame (z == n).
        const FloatP woLx = dot(wo, tx), woLy = dot(wo, ty), woLz = dot(wo, n);

        const FloatP index = m_index->Evaluate(dg, sw, active);

        // Transmitted direction (Snell), local frame. entering = dot(wo, n) > 0.
        const MaskP entering = woLz > FloatP(0.f);
        const FloatP eta = select(entering, FloatP(1.f) / index, index);
        const FloatP sini2 = max(FloatP(0.f), FloatP(1.f) - woLz * woLz);
        const FloatP sint2 = eta * eta * sini2;
        FloatP cost = sqrt(max(FloatP(0.f), FloatP(1.f) - sint2));
        cost = select(entering, -cost, cost);
        const Vector3fP wiTransmit = -eta * woLx * tx - eta * woLy * ty + cost * n;

        // Reflected direction (local): (-x, -y, z).
        const Vector3fP wiReflect = -woLx * tx - woLy * ty + woLz * n;

        // Fresnel split. F is wavelength-independent for non-dispersive glass.
        const SWCSpectrumP F = DielectricFresnel(woLz, index);
        const FloatP Fscalar = F.Average();
        const MaskP doReflect = u2 < Fscalar;

        const SWCSpectrumP kr = m_kr->Evaluate(dg, sw, active).Clamped();
        const SWCSpectrumP kt = m_kt->Evaluate(dg, sw, active).Clamped();

        // f excludes the geometric |cos| (delta lobes). Transmit carries the
        // solid-angle compression |wo.z / cost|.
        const FloatP costSafe = select(abs(cost) > FloatP(1e-8f), abs(cost),
                                       FloatP(1e-8f));
        const SWCSpectrumP fReflect = kr * F;
        const SWCSpectrumP fTransmit = kt * (SWCSpectrumP(FloatP(1.f)) - F) *
                                       SWCSpectrumP(abs(woLz) / costSafe);

        const Vector3fP wi = select(doReflect, wiReflect, wiTransmit);
        const FloatP pdf = select(doReflect, Fscalar, FloatP(1.f) - Fscalar);

        enoki::masked(s->wo, active) = wi;
        enoki::masked(s->pdf, active) = pdf;
        enoki::masked(s->f, active) = select(doReflect, fReflect, fTransmit);
        enoki::masked(s->eta, active) = select(doReflect, FloatP(1.f), eta);
        enoki::masked(s->sampledType, active) = select(doReflect,
                                                       UInt32P(uint32_t(BSDFType::SpecularReflection)),
                                                       UInt32P(uint32_t(BSDFType::SpecularTransmission)));
        enoki::masked(s->specular, active) = MaskP(true);
    }

    std::shared_ptr<Material> GlassMaterial::CreateMaterial(const PluginContext &ctx)
    {
        auto resolveColor = [&ctx](const char *name) -> std::shared_ptr<ColorTexture>
        {
            const std::string texName = ctx.params->FindTexture(name);
            if (!texName.empty() && ctx.colorTextures)
            {
                auto it = ctx.colorTextures->find(texName);
                if (it != ctx.colorTextures->end())
                    return it->second;
            }
            const RGBColor &rgb = ctx.params->FindOneRGBColor(name, RGBColor(1.f));
            return std::make_shared<ConstantColorTexture>(
                RGBColorP(FloatP(rgb.r()), FloatP(rgb.g()), FloatP(rgb.b())));
        };

        std::shared_ptr<FloatTexture> index;
        const std::string idxName = ctx.params->FindTexture("index");
        if (!idxName.empty() && ctx.floatTextures)
        {
            auto it = ctx.floatTextures->find(idxName);
            if (it != ctx.floatTextures->end())
                index = it->second;
        }
        if (!index)
        {
            index = std::make_shared<ConstantFloatTexture>(
                FloatP(ctx.params->FindOneFloat("index", 1.5f)));
        }

        return std::make_shared<GlassMaterial>(resolveColor("Kr"), resolveColor("Kt"),
                                               std::move(index));
    }

    LUX2_REGISTER_MATERIAL(GlassMaterial, "glass");

} // namespace lux2
