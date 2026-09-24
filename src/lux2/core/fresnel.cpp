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

#include "core/fresnel.h"

#include "core/texture.h"

namespace lux2
{

    namespace
    {

        // Component wise divide.
        SWCSpectrumP guarded_div(const SWCSpectrumP &num, const SWCSpectrumP &den)
        {
            return select(den != SWCSpectrumP(0.f), num / den, SWCSpectrumP(0.f));
        }

    } // namespace

    SWCSpectrumP FrDiel(const FloatP &cosi, const FloatP &cost,
                        const SWCSpectrumP &etai, const SWCSpectrumP &etat)
    {
        return FrDiel2(cosi, SWCSpectrumP(cost), guarded_div(etat, etai));
    }

    SWCSpectrumP FrDiel2(const FloatP &cosi, const SWCSpectrumP &cost,
                         const SWCSpectrumP &eta)
    {
        const SWCSpectrumP cosiS(cosi);
        SWCSpectrumP Rparl = eta * cosiS;
        Rparl = guarded_div(cost - Rparl, cost + Rparl);
        SWCSpectrumP Rperp = eta * cost;
        Rperp = guarded_div(cosiS - Rperp, cosiS + Rperp);
        return (Rparl * Rparl + Rperp * Rperp) * 0.5f;
    }

    SWCSpectrumP FrCond(const FloatP &cosi, const SWCSpectrumP &eta,
                        const SWCSpectrumP &k)
    {
        const SWCSpectrumP one(1.f);
        const SWCSpectrumP cosiS(cosi);
        const SWCSpectrumP eta2k2 = eta * eta + k * k;
        const SWCSpectrumP two_eta_cos = (SWCSpectrumP(2.f) * eta) * cosiS;

        const SWCSpectrumP tmp = eta2k2 * (cosiS * cosiS) + one;
        const SWCSpectrumP Rparl2 = guarded_div(tmp - two_eta_cos, tmp + two_eta_cos);

        const SWCSpectrumP tmp_f = eta2k2 + cosiS * cosiS;
        const SWCSpectrumP Rperp2 = guarded_div(tmp_f - two_eta_cos, tmp_f + two_eta_cos);

        return (Rparl2 + Rperp2) * 0.5f;
    }

    SWCSpectrumP FrFull(const FloatP &cosi, const SWCSpectrumP &cost,
                        const SWCSpectrumP &eta, const SWCSpectrumP &k)
    {
        const SWCSpectrumP cosiS(cosi);
        const SWCSpectrumP eta2k2 = eta * eta + k * k;
        const SWCSpectrumP two_cos_cos = (SWCSpectrumP(2.f) * cosiS) * cost;
        const SWCSpectrumP common = two_cos_cos * eta;

        const SWCSpectrumP tmp = eta2k2 * (cosiS * cosiS) + cost * cost;
        const SWCSpectrumP Rparl2 = guarded_div(tmp - common, tmp + common);

        const SWCSpectrumP tmp_f = eta2k2 * (cost * cost) + cosiS * cosiS;
        const SWCSpectrumP Rperp2 = guarded_div(tmp_f - common, tmp_f + common);

        return (Rparl2 + Rperp2) * 0.5f;
    }

    SWCSpectrumP FresnelApproxEta(const SWCSpectrumP &Fr)
    {
        const SWCSpectrumP one(1.f);
        const SWCSpectrumP sqrtR = sqrt(clamp(Fr, SWCSpectrumP(0.f), SWCSpectrumP(0.999f)));
        return (one + sqrtR) / (one - sqrtR);
    }

    SWCSpectrumP FresnelApproxK(const SWCSpectrumP &Fr)
    {
        const SWCSpectrumP one(1.f);
        const SWCSpectrumP r = clamp(Fr, SWCSpectrumP(0.f), SWCSpectrumP(0.999f));
        return SWCSpectrumP(2.f) * sqrt(r / (one - r));
    }

    SWCSpectrumP FresnelGeneralEvaluate(const FresnelGeneralP &fg, const FloatP &cosi)
    {
        if (fg.model == FresnelModel::Conductor)
            return FrCond(cosi, fg.eta, fg.k);

        // FULL path: derive cos(theta_t) from Snell + complex IOR, then FrFull.
        const SWCSpectrumP one(1.f);
        const SWCSpectrumP sint2 = clamp(one - SWCSpectrumP(cosi * cosi),
                                         SWCSpectrumP(0.f), one);
        const SWCSpectrumP cost2 = one - sint2;
        const SWCSpectrumP a = SWCSpectrumP(2.f) * fg.k * fg.k * sint2;
        const SWCSpectrumP cost = sqrt((cost2 + sqrt(cost2 * cost2 + a * a)) *
                                       SWCSpectrumP(0.5f));
        return FrFull(cosi, cost, fg.eta, fg.k);
    }

} // namespace lux2
