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

#include "lights/infinite.h"

#include "core/bsdf_type.h"
#include "core/dynload.h"
#include "core/math.h"
#include "core/paramset.h"
#include "core/register.h"
#include "textures/constant.h"

#include <enoki/array.h>

namespace lux2
{

    namespace
    {
        // Distance used to place a finite light position along the sampled direction.
        constexpr float kInfiniteDistance = 1e30f;
    } // namespace

    InfiniteLight::InfiniteLight(std::shared_ptr<ColorTexture> Le, float gain)
        : m_Le(std::move(Le)), m_gain(gain) {}

    uint32_t InfiniteLight::flags() const
    {
        // Environment illumination is treated as a diffuse reflection lobe for
        // MIS weighting.
        return uint32_t(BSDFType::DiffuseReflection);
    }

    SWCSpectrumP InfiniteLight::Le(const RayP &ray, MaskP active) const
    {
        // Build a dummy shading record at the origin and
        // evaluate the emission texture at the ray's SWA wavelength.
        DifferentialGeometryP dg;
        dg.p = Point3fP(FloatP(0.f), FloatP(0.f), FloatP(0.f));
        dg.n = Normal3fP(FloatP(0.f), FloatP(0.f), FloatP(1.f));
        dg.uv_u = FloatP(0.f);
        dg.uv_v = FloatP(0.f);

        SpectrumWavelengthsP sw;
        sw.FromWavelength(ray.wavelengths);
        return m_Le->Evaluate(dg, sw, active) * FloatP(m_gain);
    }

    SWCSpectrumP InfiniteLight::Le(const DifferentialGeometryP &,
                                   const SpectrumWavelengthsP &sw,
                                   MaskP active) const
    {
        // Return the constant value so the interface is satisfied.
        DifferentialGeometryP dg;
        dg.p = Point3fP(FloatP(0.f), FloatP(0.f), FloatP(0.f));
        dg.n = Normal3fP(FloatP(0.f), FloatP(0.f), FloatP(1.f));
        dg.uv_u = FloatP(0.f);
        dg.uv_v = FloatP(0.f);
        return m_Le->Evaluate(dg, sw, active) * FloatP(m_gain);
    }

    MaskP InfiniteLight::Sample_L(const SpectrumWavelengthsP &sw,
                                  const Point3fP &p, const Normal3fP &n,
                                  const FloatP &u0, const FloatP &u1,
                                  const FloatP &u2,
                                  Point3fP *lightP, Vector3fP *wi,
                                  Normal3fP *lightN,
                                  FloatP *pdf, SWCSpectrumP *LeOut,
                                  MaskP active) const
    {
        // Uniform sphere sampling: cosT = 1 - 2*u0 in [-1,1], phi = 2*pi*u1.
        const FloatP cosT = FloatP(1.f) - FloatP(2.f) * u0;
        const FloatP sinT = enoki::sqrt(enoki::max(FloatP(0.f),
                                                   FloatP(1.f) - cosT * cosT));
        const FloatP phi = FloatP(2.f * PI) * u1;
        const FloatP sinPhi = enoki::sin(phi);
        const FloatP cosPhi = enoki::cos(phi);

        const Vector3fP dir(sinT * cosPhi, sinT * sinPhi, cosT);

        enoki::masked(*wi, active) = dir;
        enoki::masked(*lightP, active) = p + dir * FloatP(kInfiniteDistance);
        // The environment's surface normal at the sampled direction is +wi.
        enoki::masked(*lightN, active) = Normal3fP(dir.x(), dir.y(), dir.z());
        enoki::masked(*pdf, active) = FloatP(INV_FOURPI);

        // Radiance along the sampled direction.
        DifferentialGeometryP dg;
        dg.p = Point3fP(FloatP(0.f), FloatP(0.f), FloatP(0.f));
        dg.n = Normal3fP(FloatP(0.f), FloatP(0.f), FloatP(1.f));
        dg.uv_u = FloatP(0.f);
        dg.uv_v = FloatP(0.f);
        enoki::masked(*LeOut, active) =
            m_Le->Evaluate(dg, sw, active) * FloatP(m_gain);

        // A uniform environment is always valid.
        return active;
    }

    FloatP InfiniteLight::Pdf_L(const Point3fP &, const Normal3fP &,
                                const Point3fP &, const Normal3fP &,
                                MaskP active) const
    {
        return enoki::select(active, FloatP(INV_FOURPI), FloatP(0.f));
    }

    std::shared_ptr<Light> InfiniteLight::CreateLight(const PluginContext &ctx)
    {
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
            le = std::make_shared<ConstantColorTexture>(rgbP, /*illuminant=*/true);
        }

        const float gain = ctx.params ? ctx.params->FindOneFloat("gain", 1.f) : 1.f;
        return std::make_shared<InfiniteLight>(std::move(le), gain);
    }

    LUX2_REGISTER_LIGHT(InfiniteLight, "infinite");

} // namespace lux2
