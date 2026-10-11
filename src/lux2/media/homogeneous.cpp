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

#include "core/medium.h"
#include "core/color.h"
#include "core/dynload.h"
#include "core/math.h"
#include "core/paramset.h"
#include "core/register.h"

#include <enoki/array.h>

#include <limits>

namespace lux2
{
    namespace
    {
        // Schlick k from anisotropy g.
        inline FloatP SchlickK(float g)
        {
            return FloatP(g * (1.55f - 0.55f * g * g));
        }

        // Broadcast a RGB color to all lanes.
        inline RGBColorP Broadcast(const RGBColor &c)
        {
            return RGBColorP(FloatP(c.r()), FloatP(c.g()), FloatP(c.b()));
        }
    } // namespace

    class HomogeneousMedium : public Medium
    {
    public:
        HomogeneousMedium(const RGBColor &sigmaA, const RGBColor &sigmaS,
                          float g, const RGBColor &Le)
            : m_sigmaA(sigmaA), m_sigmaS(sigmaS), m_g(g), m_Le(Le)
        {
            m_emissive = !(Le.r() == 0.f && Le.g() == 0.f && Le.b() == 0.f);
        }

        static std::shared_ptr<Medium> CreateMedium(const PluginContext &ctx);

        uint32_t Flags() const override
        {
            return MF_Analytic | (m_emissive ? MF_Emissive : 0u);
        }

        SWCSpectrumP SigmaA(const SpectrumWavelengthsP &sw, const Point3fP &,
                            MaskP) const override
        {
            // Reflectant Smits basis.
            return RGBToSmitsSPD(Broadcast(m_sigmaA), sw, false);
        }

        SWCSpectrumP SigmaS(const SpectrumWavelengthsP &sw, const Point3fP &,
                            MaskP) const override
        {
            return RGBToSmitsSPD(Broadcast(m_sigmaS), sw, false);
        }

        SWCSpectrumP Lve(const SpectrumWavelengthsP &sw, const Point3fP &,
                         MaskP) const override
        {
            if (!m_emissive)
                return SWCSpectrumP(0.f);
            // Emission is radiance, illuminant basis.
            return RGBToSmitsSPD(Broadcast(m_Le), sw, true);
        }

        void SampleInteraction(const SpectrumWavelengthsP &sw, const RayP &ray,
                               const FloatP &t0, const FloatP &t1,
                               const FloatP &u_flight, RNGP &,
                               MediumSampleP *out, MaskP active) const override
        {
            if (none(active))
                return;

            const SWCSpectrumP sa = SigmaA(sw, Point3fP(0.f), active);
            const SWCSpectrumP ss = SigmaS(sw, Point3fP(0.f), active);

            // Guard segment length against inf.
            FloatP len = t1 - t0;
            len = enoki::min(len, FloatP(kMaxSegment));
            len = enoki::max(len, FloatP(0.f));

            // Sample flight at rate sigma_s where d = -log(1-u)/sigma_s. Where
            // sigma_s ~ 0 the lane never scatters (d -> INF).
            const FloatP u = enoki::min(u_flight, FloatP(1.f) - FloatP(1e-6f));
            const FloatP negLog = -enoki::log(FloatP(1.f) - u);
            const MaskP scattering = ss > FloatP(EPS_DENOM);
            const FloatP dSample =
                select(scattering, negLog / enoki::max(ss, FloatP(EPS_DENOM)),
                       FloatP(std::numeric_limits<float>::infinity()));

            // Event occurs only if the sampled distance is within the segment.
            const MaskP didScatter = active && (dSample < len);
            const FloatP dEff = select(didScatter, dSample, len);

            // Absorption weight exp(-sigma_a * dEff). Guard sigma_a==0 to avoid
            // 0*INF -> NaN when the segment is unbounded.
            const SWCSpectrumP aLen =
                select(sa > FloatP(0.f), sa * dEff, SWCSpectrumP(0.f));
            const SWCSpectrumP weight = enoki::exp(-aLen);

            // Emission: Lve * (1 - exp(-sigma_a*dEff)) / sigma_a, limit
            // Lve*dEff as sigma_a -> 0.
            if (m_emissive)
            {
                const SWCSpectrumP Le = Lve(sw, Point3fP(0.f), active);
                const SWCSpectrumP oneMinusE = FloatP(1.f) - weight;
                const SWCSpectrumP ratio =
                    select(sa > FloatP(EPS_DENOM),
                           oneMinusE / enoki::max(sa, FloatP(EPS_DENOM)),
                           dEff);
                enoki::masked(out->emission, active) = Le * ratio;
            }

            enoki::masked(out->d, active) = dEff;
            enoki::masked(out->scattered, active) = didScatter;
            enoki::masked(out->weight, active) = weight;
            enoki::masked(out->sigma_s, active) = ss;
            enoki::masked(out->sigma_t, active) = sa + ss;
        }

        SWCSpectrumP Transmittance(const SpectrumWavelengthsP &sw,
                                   const RayP &ray, const FloatP &t0,
                                   const FloatP &t1, RNGP &,
                                   MaskP active) const override
        {
            if (none(active))
                return SWCSpectrumP(1.f);
            const SWCSpectrumP st =
                SigmaA(sw, Point3fP(0.f), active) +
                SigmaS(sw, Point3fP(0.f), active);
            FloatP len = enoki::min(t1 - t0, FloatP(kMaxSegment));
            len = enoki::max(len, FloatP(0.f));
            const SWCSpectrumP tau =
                select(st > FloatP(0.f), st * len, SWCSpectrumP(0.f));
            return select(active, enoki::exp(-tau), SWCSpectrumP(1.f));
        }

        SWCSpectrumP Tau(const RayP &ray, const FloatP &t0, const FloatP &t1,
                         MaskP active) const override
        {
            if (none(active))
                return SWCSpectrumP(0.f);
            SpectrumWavelengthsP sw;
            sw.FromWavelength(ray.wavelengths);
            const SWCSpectrumP st = SigmaA(sw, Point3fP(0.f), active) +
                                    SigmaS(sw, Point3fP(0.f), active);
            FloatP len = enoki::max(t1 - t0, FloatP(0.f));
            return select(active, st * len, SWCSpectrumP(0.f));
        }

        void PhaseSample(const Vector3fP &wo, const FloatP &u1,
                         const FloatP &u2, Vector3fP *wi, FloatP *pdf,
                         MaskP active) const override
        {
            if (none(active))
                return;
            const FloatP k = SchlickK(m_g);

            // Invert CDF of p(mu) = (1 + k*mu)/2 over mu in [-1,1]:
            //   mu = (-1 + sqrt((1-k)^2 + 4*k*u)) / k, limit 2u-1 as k->0.
            const FloatP disc = enoki::max(
                (FloatP(1.f) - k) * (FloatP(1.f) - k) + FloatP(4.f) * k * u1,
                FloatP(0.f));
            const FloatP muK =
                (FloatP(-1.f) + enoki::sqrt(disc)) /
                select(enoki::abs(k) > FloatP(EPS_DENOM), k, FloatP(1.f));
            const FloatP mu =
                select(enoki::abs(k) > FloatP(EPS_DENOM), muK,
                       FloatP(2.f) * u1 - FloatP(1.f));

            // Build a direction at polar angle acos(mu) about wo.
            const FloatP phi = FloatP(2.f * PI) * u2;
            const FloatP sinT = enoki::sqrt(enoki::max(FloatP(1.f) - mu * mu,
                                                       FloatP(0.f)));
            Vector3fP dir = frameDirection(wo, mu, sinT * enoki::cos(phi),
                                           sinT * enoki::sin(phi));

            const FloatP p = (FloatP(1.f) + k * mu) * FloatP(INV_FOURPI);
            enoki::masked(*wi, active) = dir;
            enoki::masked(*pdf, active) = p;
        }

        FloatP PhasePdf(const Vector3fP &wo, const Vector3fP &wi,
                        MaskP active) const override
        {
            const FloatP k = SchlickK(m_g);
            const FloatP cosT = dot(wo, wi);
            const FloatP p = (FloatP(1.f) + k * cosT) * FloatP(INV_FOURPI);
            return select(active, enoki::max(p, FloatP(0.f)), FloatP(0.f));
        }

    private:
        // Build a unit direction from (mu, sx, sy) in an orthonormal frame
        // whose +z axis is wo. (sx, sy) are the tangential components with
        // mu^2 + sx^2 + sy^2 == 1.
        static Vector3fP frameDirection(const Vector3fP &wo, const FloatP &mu,
                                        const FloatP &sx, const FloatP &sy)
        {
            auto [b, c] = coordinate_system(wo);
            return mu * wo + sx * b + sy * c;
        }

        RGBColor m_sigmaA, m_sigmaS, m_Le;
        float m_g = 0.f;
        bool m_emissive = false;

        // Upper bound on a segment length used in exponents.
        static constexpr float kMaxSegment = 1e6f;
    };

    std::shared_ptr<Medium> HomogeneousMedium::CreateMedium(
        const PluginContext &ctx)
    {
        ParamSet *p = ctx.params;
        const RGBColor sa =
            p ? p->FindOneRGBColor("sigma_a", RGBColor(0.f)) : RGBColor(0.f);
        const RGBColor ss =
            p ? p->FindOneRGBColor("sigma_s", RGBColor(0.f)) : RGBColor(0.f);
        const float g = p ? p->FindOneFloat("g", 0.f) : 0.f;
        const RGBColor le =
            p ? p->FindOneRGBColor("Le", RGBColor(0.f)) : RGBColor(0.f);
        return std::make_shared<HomogeneousMedium>(sa, ss, g, le);
    }

} // namespace lux2

LUX2_REGISTER_MEDIUM(lux2::HomogeneousMedium, "homogeneous");
