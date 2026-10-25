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

#include "lights/point.h"

#include "core/error.h"
#include "core/math/math.h"
#include "core/paramset.h"
#include "core/register.h"
#include "core/texture.h"
#include "lights/light_photometric.h"
#include "textures/constant.h"

#include <enoki/array.h>

namespace lux2
{

    PointLight::PointLight(std::shared_ptr<ColorTexture> L, float baseGain,
                           float power, float efficacy, const Point3f &worldPos)
        : m_L(std::move(L)), m_pos(worldPos)
    {
        // Legacy gainFactor = power*efficacy / (4*PI * Y). Applied only when
        // finite and > 0; a point has no bind step so it is resolved here.
        bool applied = false;
        const float gf = PhotometricGainFactor(
            power, efficacy, 4.f * PI, m_L->IntegratedLuminance(), applied);
        if (!applied && (power != 0.f || efficacy != 0.f))
            LOG(LUX_WARNING, LUX_SYSTEM)
                << "PointLight: power/efficacy requested but emission "
                   "luminance is unknown; normalization skipped.";
        m_gain = baseGain * gf;
    }

    MaskP PointLight::Sample_Li(const SpectrumWavelengthsP &sw,
                                const Point3fP &p, const Normal3fP &,
                                const FloatP &, const FloatP &, const FloatP &,
                                Point3fP *lightP, Vector3fP *wi,
                                Normal3fP *lightN, FloatP *pdf,
                                SWCSpectrumP *LeOut, MaskP active,
                                MaskP) const
    {
        // Broadcast the world position to every lane.
        const Point3fP lp(FloatP(m_pos.x()), FloatP(m_pos.y()),
                          FloatP(m_pos.z()));

        const Vector3fP delta(lp - p);
        const FloatP distSq = enoki::dot(delta, delta);
        const FloatP dist = enoki::sqrt(distSq);
        const Vector3fP w = delta / max(dist, FloatP(EPS_DENOM)); // p -> light

        // A point emits in all directions; only the degenerate coincidence
        // with the receiver is invalid.
        const MaskP valid = active && (dist > FloatP(EPS_DENOM));

        enoki::masked(*lightP, active) = lp;
        enoki::masked(*wi, active) = w;
        // No meaningful light normal for a point.
        enoki::masked(*lightN, active) = Normal3fP(FloatP(0.f));
        // Dirac positional pdf: constant 1.
        enoki::masked(*pdf, active) = FloatP(1.f);

        // Radiance carries the inverse-square falloff.
        const SWCSpectrumP base = m_L->Evaluate(DifferentialGeometryP(), sw,
                                                active);
        const FloatP invDistSq =
            select(distSq > FloatP(EPS_DENOM),
                   FloatP(1.f) / max(distSq, FloatP(EPS_DENOM)), FloatP(0.f));
        enoki::masked(*LeOut, active) = base * (FloatP(m_gain) * invDistSq);

        return valid;
    }

    float PointLight::Power() const
    {
        // Lux Lbase->Y() * gain * 4pi.
        const std::optional<float> Y = m_L->IntegratedLuminance();
        if (!Y.has_value())
            return m_gain;
        return 4.f * PI * (*Y) * m_gain;
    }

    std::shared_ptr<Light> PointLight::CreateLight(const PluginContext &ctx)
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
            const RGBColorP rgbP(FloatP(rgb.r()), FloatP(rgb.g()),
                                 FloatP(rgb.b()));
            le = std::make_shared<ConstantColorTexture>(rgbP, /*illuminant=*/true);
        }

        const float gain = ctx.params ? ctx.params->FindOneFloat("gain", 1.f) : 1.f;
        // Lux PointLight defaults.
        const float power = ctx.params ? ctx.params->FindOneFloat("power", 0.f) : 0.f;
        const float efficacy =
            ctx.params ? ctx.params->FindOneFloat("efficacy", 0.f) : 0.f;

        // World position is the authored `from` point under the light transform.
        const Point3f from =
            ctx.params ? ctx.params->FindOnePoint("from", Point3f(0.f, 0.f, 0.f))
                       : Point3f(0.f, 0.f, 0.f);
        const Point3f worldPos = ctx.transform * from;

        auto light = std::make_shared<PointLight>(std::move(le), gain, power,
                                                  efficacy, worldPos);
        // Importance hint for power/importance weighting.
        const float importance =
            ctx.params ? ctx.params->FindOneFloat("importance", 1.f) : 1.f;
        light->SetImportance(importance);
        return light;
    }

    LUX2_REGISTER_LIGHT(PointLight, "point");

} // namespace lux2
