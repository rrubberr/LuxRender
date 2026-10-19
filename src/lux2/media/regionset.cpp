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

#include "media/regionset.h"

#include "core/math.h"
#include "core/phase.h"

#include <enoki/array.h>

#include <algorithm>
#include <cassert>

namespace lux2
{
    namespace
    {
        // Local space slab test of ray (o,d) against box b, clipped to [t0,t1].
        // eps widens the box faces in local space. Over-widening only
        // inflates the majorant (extra null collisions), never bias; it
        // keeps the slab test from rejecting a point Contains() accepts.
        inline void SlabInterval(const Point3fP &o, const Vector3fP &d,
                                 const BBox &b, const FloatP &t0,
                                 const FloatP &t1, FloatP *tNear, FloatP *tFar,
                                 MaskP *hit, MaskP active, Float eps = 0.f)
        {
            FloatP tn = t0, tf = t1;
            MaskP ok = active;
            const Vector3fP e(eps, eps, eps);
            const Point3fP omin = b.pMin - e;
            const Point3fP omax = b.pMax + e;

            for (int a = 0; a < 3; ++a)
            {
                const FloatP od = a == 0 ? o.x() : (a == 1 ? o.y() : o.z());
                const FloatP dd = a == 0 ? d.x() : (a == 1 ? d.y() : d.z());
                const FloatP bmin =
                    a == 0 ? omin.x() : (a == 1 ? omin.y() : omin.z());
                const FloatP bmax =
                    a == 0 ? omax.x() : (a == 1 ? omax.y() : omax.z());

                const MaskP zero = dd == FloatP(0.f);
                // Parallel and outside the slab => miss.
                const MaskP parOk = (od >= bmin) && (od <= bmax);

                const FloatP inv = select(zero, FloatP(0.f), enoki::rcp(dd));
                FloatP tA = (bmin - od) * inv;
                FloatP tB = (bmax - od) * inv;
                const MaskP swap = tA > tB;
                const FloatP lo = select(swap, tB, tA);
                const FloatP hi = select(swap, tA, tB);

                // Skip the update on parallel axes so tf is not forced to 0.
                tn = select(zero, tn, enoki::max(tn, lo));
                tf = select(zero, tf, enoki::min(tf, hi));
                ok = ok && select(zero, parOk, MaskP(true));
            }

            *hit = ok && (tn < tf) && (tf > t0);
            *tNear = enoki::clamp(tn, t0, t1);
            *tFar = enoki::clamp(tf, t0, t1);
        }
    } // namespace

    void RegionSetMedium::AddChild(const std::shared_ptr<HomogeneousMedium> &c)
    {
        // A region child must carry a bound: the composite relies on
        // Contains()/LocalBox() to decide membership and to clip the ray.
        assert(c->IsBounded());
        Child ch;
        ch.med = c;
        ch.sigmaTMaxRGB = c->SigmaTMaxRGB();
        {
            // Local-space slack covering the ulp gap between the slab test
            // (W2L*o + t*(W2L*d)) and Contains (W2L*(o + t*d)).
            const BBox lb = c->LocalBox();
            const Vector3f d = lb.Diagonal();
            const Float m = std::max({std::abs(lb.pMin.x()), std::abs(lb.pMax.x()),
                                      std::abs(lb.pMin.y()), std::abs(lb.pMax.y()),
                                      std::abs(lb.pMin.z()), std::abs(lb.pMax.z()),
                                      Float(1.f)});
            ch.slabEps = 1e-5f * m + 1e-7f * std::max({d.x(), d.y(), d.z()});
        }
        ch.k = c->K();
        m_children.push_back(std::move(ch));

        const BBox wb = c->Bounds();
        m_worldUnion = Union(m_worldUnion, wb);
        if (c->Flags() & MF_Emissive)
            m_emissive = true;
    }

    uint32_t RegionSetMedium::Flags() const
    {
        return MF_Tracked | MF_Bounded | (m_emissive ? MF_Emissive : 0u);
    }

    void RegionSetMedium::IntersectAABB(const RayP &ray, FloatP *t0, FloatP *t1,
                                        MaskP *hit, MaskP active) const
    {
        // Union AABB test only.
        SlabInterval(ray.o, ray.d, m_worldUnion, ray.mint,
                     FloatP(std::numeric_limits<float>::infinity()), t0, t1,
                     hit, active);
    }

    void RegionSetMedium::AccumulateChildren(
        const SpectrumWavelengthsP &sw, const Point3fP &p, MaskP active,
        SWCSpectrumP *sa, SWCSpectrumP *ss, SWCSpectrumP *Le) const
    {
        if (sa)
            *sa = SWCSpectrumP(0.f);
        if (ss)
            *ss = SWCSpectrumP(0.f);
        if (Le)
            *Le = SWCSpectrumP(0.f);
        for (const auto &ch : m_children)
        {
            const MaskP in = active && ch.med->Contains(p);
            if (none(in))
                continue;
            if (sa)
                *sa = select(in, *sa + ch.med->SigmaA(sw, p, in), *sa);
            if (ss)
                *ss = select(in, *ss + ch.med->SigmaS(sw, p, in), *ss);
            if (Le)
                *Le = select(in, *Le + ch.med->Lve(sw, p, in), *Le);
        }
    }

    void RegionSetMedium::ChildSigmaSWeights(
        const SpectrumWavelengthsP &sw, const Point3fP &p, MaskP active,
        std::vector<FloatP> *w, FloatP *sumW) const
    {
        const std::size_t n = m_children.size();
        w->assign(n, FloatP(0.f));
        FloatP sum(0.f);
        for (std::size_t i = 0; i < n; ++i)
        {
            const MaskP in = active && m_children[i].med->Contains(p);
            const FloatP wi =
                select(in, m_children[i].med->SigmaS(sw, p, in), FloatP(0.f));
            (*w)[i] = wi;
            sum = sum + wi;
        }
        *sumW = sum;
    }

    SWCSpectrumP RegionSetMedium::SigmaA(const SpectrumWavelengthsP &sw,
                                         const Point3fP &p,
                                         MaskP active) const
    {
        SWCSpectrumP sa;
        AccumulateChildren(sw, p, active, &sa, nullptr, nullptr);
        return sa;
    }

    SWCSpectrumP RegionSetMedium::SigmaS(const SpectrumWavelengthsP &sw,
                                         const Point3fP &p,
                                         MaskP active) const
    {
        SWCSpectrumP ss;
        AccumulateChildren(sw, p, active, nullptr, &ss, nullptr);
        return ss;
    }

    SWCSpectrumP RegionSetMedium::Lve(const SpectrumWavelengthsP &sw,
                                      const Point3fP &p, MaskP active) const
    {
        if (!m_emissive)
            return SWCSpectrumP(0.f);
        SWCSpectrumP Le;
        AccumulateChildren(sw, p, active, nullptr, nullptr, &Le);
        return Le;
    }

    void RegionSetMedium::Coeffs(const SpectrumWavelengthsP &sw,
                                 const Point3fP &p, MaskP active,
                                 MediumCoeffs *out) const
    {
        AccumulateChildren(sw, p, active, &out->sa, &out->ss, &out->Le);
    }

    FloatP RegionSetMedium::PhaseK(const SpectrumWavelengthsP &sw,
                                   const Point3fP &p, const FloatP &u,
                                   MaskP active) const
    {
        std::vector<FloatP> w;
        FloatP sumW(0.f);
        ChildSigmaSWeights(sw, p, active, &w, &sumW);

        // sigma_s-weighted CDF draw.
        const FloatP target = u * sumW;
        FloatP kOut(0.f), lastK(0.f);
        FloatP acc(0.f);
        MaskP found(false);
        for (std::size_t i = 0; i < m_children.size(); ++i)
        {
            const FloatP wi = w[i];
            lastK = select(wi > FloatP(0.f), FloatP(m_children[i].k), lastK);
            acc = acc + wi;
            const MaskP pick = active && !found && (target < acc);
            kOut = select(pick, FloatP(m_children[i].k), kOut);
            found = found || pick;
        }
        // sumW == 0 (no scattering child at p) falls back to isotropic.
        return select(sumW > FloatP(0.f), select(found, kOut, lastK),
                      FloatP(0.f));
    }

    FloatP RegionSetMedium::PhasePdf(const SpectrumWavelengthsP &sw,
                                     const Point3fP &p, const Vector3fP &wo,
                                     const Vector3fP &wi, MaskP active) const
    {
        // Marginal mixture q = sum (sigma_s,i / sum sigma_s) * p_i.
        std::vector<FloatP> w;
        FloatP sumW(0.f);
        ChildSigmaSWeights(sw, p, active, &w, &sumW);

        FloatP q(0.f);
        for (std::size_t i = 0; i < m_children.size(); ++i)
            q = q + w[i] * SchlickPdf(FloatP(m_children[i].k), wo, wi);
        const FloatP inv =
            select(sumW > FloatP(0.f), enoki::rcp(sumW), FloatP(0.f));
        return select(active, q * inv, FloatP(0.f));
    }

    void RegionSetMedium::MajorantBegin(const RayP &ray, const FloatP &t0,
                                        const FloatP &t1, MajorantState &ms,
                                        MaskP active) const
    {
        FloatP unionT0 = FloatP(std::numeric_limits<float>::infinity());
        FloatP unionT1 = FloatP(-std::numeric_limits<float>::infinity());
        FloatP sigBar(0.f);
        MaskP anyHit(false);

        for (const auto &ch : m_children)
        {
            // Transform the ray into local space.
            const Point3fP oL = ch.med->WorldToLocal() * ray.o;
            const Vector3fP dL = ch.med->WorldToLocal() * ray.d;

            FloatP tn, tf;
            MaskP hit(false);
            SlabInterval(oL, dL, ch.med->LocalBox(), t0, t1, &tn, &tf, &hit,
                         active, ch.slabEps);

            unionT0 = select(hit, enoki::min(unionT0, tn), unionT0);
            unionT1 = select(hit, enoki::max(unionT1, tf), unionT1);
            sigBar = select(hit, sigBar + FloatP(ch.sigmaTMaxRGB), sigBar);
            anyHit = anyHit || hit;
        }

        // Lanes missing every box.
        const MaskP have = anyHit && (unionT0 < unionT1);
        ms.tStart = select(have, unionT0, t0);
        ms.tEnd = select(have, unionT1, t0);
        ms.sigBar = select(have, sigBar, FloatP(0.f));
    }

    MaskP RegionSetMedium::MajorantNext(MajorantState &ms, MaskP active) const
    {
        static_cast<void>(ms);
        static_cast<void>(active);
        return MaskP(false); // single segment producer
    }

} // namespace lux2
