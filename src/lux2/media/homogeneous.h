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

#ifndef LUX2_MEDIA_HOMOGENEOUS_H
#define LUX2_MEDIA_HOMOGENEOUS_H

#include "core/medium.h"
#include "core/bbox.h"
#include "core/color.h"
#include "core/math.h"
#include "core/phase.h"
#include "core/transform.h"

#include <enoki/array.h>

#include <algorithm>
#include <limits>
#include <memory>

namespace lux2
{
    struct PluginContext;

    // Broadcast an RGB color to all lanes.
    inline RGBColorP BroadcastRGB(const RGBColor &c)
    {
        return RGBColorP(FloatP(c.r()), FloatP(c.g()), FloatP(c.b()));
    }

    // A constant coefficient medium.
    class HomogeneousMedium : public Medium
    {
    public:
        HomogeneousMedium(const RGBColor &sigmaA, const RGBColor &sigmaS,
                          float g, const RGBColor &Le)
            : m_sigmaA(sigmaA), m_sigmaS(sigmaS), m_Le(Le),
              m_k(SchlickK(g))
        {
            m_emissive = !(Le.r() == 0.f && Le.g() == 0.f && Le.b() == 0.f);
            // Each projected spectrum is <= its own channel max so
            // sigma_t(sw) = sa(sw)+ss(sw) <= maxChannel(sa) + maxChannel(ss).
            m_sigmaTMaxRGB = MaxChannel(sigmaA) + MaxChannel(sigmaS);
        }

        // Attach a local space box and its volume-to-world transform.
        void SetBound(const BBox &localBox, const Transform &toWorld)
        {
            m_bounded = true;
            m_localBox = localBox;
            m_worldToLocal = toWorld.inverse();
            m_worldBound = WorldAABB(localBox, toWorld);
        }

        static std::shared_ptr<Medium> CreateMedium(const PluginContext &ctx);

        uint32_t Flags() const override
        {
            return MF_Analytic | (m_emissive ? MF_Emissive : 0u) |
                   (m_bounded ? MF_Bounded : 0u);
        }

        BBox Bounds() const override { return m_worldBound; }

        // Local space containment of a world point. Always true when unbounded.
        MaskP Contains(const Point3fP &pWorld) const
        {
            if (!m_bounded)
                return MaskP(true);
            const Point3fP pl = m_worldToLocal * pWorld;
            return (pl.x() >= FloatP(m_localBox.pMin.x()) &&
                    pl.x() <= FloatP(m_localBox.pMax.x()) &&
                    pl.y() >= FloatP(m_localBox.pMin.y()) &&
                    pl.y() <= FloatP(m_localBox.pMax.y()) &&
                    pl.z() >= FloatP(m_localBox.pMin.z()) &&
                    pl.z() <= FloatP(m_localBox.pMax.z()));
        }

        // Accessors for RegionSetMedium.
        float K() const { return m_k; }
        float SigmaTMaxRGB() const { return m_sigmaTMaxRGB; }
        float LeMaxRGB() const
        {
            return std::max({m_Le.r(), m_Le.g(), m_Le.b()});
        }
        bool IsBounded() const { return m_bounded; }
        const BBox &LocalBox() const { return m_localBox; }
        const Transform &WorldToLocal() const { return m_worldToLocal; }

        // Coefficient queries carry no containment.
        SWCSpectrumP SigmaA(const SpectrumWavelengthsP &sw, const Point3fP &,
                            MaskP) const override
        {
            return RGBToSmitsSPD(BroadcastRGB(m_sigmaA), sw, false);
        }

        SWCSpectrumP SigmaS(const SpectrumWavelengthsP &sw, const Point3fP &,
                            MaskP) const override
        {
            return RGBToSmitsSPD(BroadcastRGB(m_sigmaS), sw, false);
        }

        SWCSpectrumP Lve(const SpectrumWavelengthsP &sw, const Point3fP &,
                         MaskP) const override
        {
            if (!m_emissive)
                return SWCSpectrumP(0.f);
            // Emission is radiance, illuminant basis.
            return RGBToSmitsSPD(BroadcastRGB(m_Le), sw, true);
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
            // p is only read when scattered.
            enoki::masked(out->p, active) = ray(t0 + dEff);
            enoki::masked(out->scattered, active) = didScatter;
            enoki::masked(out->weight, active) = weight;
            enoki::masked(out->sigma_s, active) = ss;
            enoki::masked(out->sigma_t, active) = sa + ss;
            enoki::masked(out->k, active) = FloatP(m_k);
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

        FloatP PhasePdf(const SpectrumWavelengthsP &, const Point3fP &,
                        const Vector3fP &wo, const Vector3fP &wi,
                        MaskP active) const override
        {
            const FloatP p = SchlickPdf(FloatP(m_k), wo, wi);
            return select(active, p, FloatP(0.f));
        }

    private:
        static float MaxChannel(const RGBColor &c)
        {
            return std::max(c.r(), std::max(c.g(), c.b()));
        }

        // World space AABB of a local box under toWorld.
        static BBox WorldAABB(const BBox &b, const Transform &toWorld)
        {
            BBox out;
            for (int i = 0; i < 8; ++i)
            {
                const Point3f c((i & 1) ? b.pMax.x() : b.pMin.x(),
                                (i & 2) ? b.pMax.y() : b.pMin.y(),
                                (i & 4) ? b.pMax.z() : b.pMin.z());
                out = Union(out, toWorld * c);
            }
            return out;
        }

        RGBColor m_sigmaA, m_sigmaS, m_Le;
        float m_k = 0.f; // Schlick k from g
        bool m_emissive = false;
        float m_sigmaTMaxRGB = 0.f; // sw independent sigma_t upper bound

        // Bounded region/child state.
        bool m_bounded = false;
        BBox m_localBox;
        Transform m_worldToLocal;
        BBox m_worldBound;

        // Upper bound on a segment length used in exponents.
        static constexpr float kMaxSegment = 1e6f;
    };

} // namespace lux2

#endif // LUX2_MEDIA_HOMOGENEOUS_H
