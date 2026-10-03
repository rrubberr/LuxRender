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
        FloatP guarded_div(const FloatP &num, const FloatP &den)
        {
            return select(neq(den, FloatP(0.f)), num / den, FloatP(0.f));
        }

        // FresnelModel is a per packet enum, so the predicates reduce
        // across all lanes.
        FresnelModel ResolveModel(const FloatP &eta, const FloatP &k)
        {
            if (all(eta > FloatP(10.f) * k))
                return FresnelModel::Dielectric;
            if (all(eta <= k))
                return FresnelModel::Conductor;
            return FresnelModel::Full;
        }

    } // namespace

    FloatP FrDiel(const FloatP &cosi, const FloatP &cost,
                  const FloatP &etai, const FloatP &etat)
    {
        return FrDiel2(cosi, cost, guarded_div(etat, etai));
    }

    FloatP FrDiel2(const FloatP &cosi, const FloatP &cost, const FloatP &eta)
    {
        FloatP Rparl = eta * cosi;
        Rparl = guarded_div(cost - Rparl, cost + Rparl);
        FloatP Rperp = eta * cost;
        Rperp = guarded_div(cosi - Rperp, cosi + Rperp);
        return (Rparl * Rparl + Rperp * Rperp) * 0.5f;
    }

    FloatP FrCond(const FloatP &cosi, const FloatP &eta, const FloatP &k)
    {
        const FloatP one(1.f);
        const FloatP eta2k2 = eta * eta + k * k;
        const FloatP two_eta_cos = (FloatP(2.f) * eta) * cosi;

        const FloatP tmp = eta2k2 * (cosi * cosi) + one;
        const FloatP Rparl2 = guarded_div(tmp - two_eta_cos, tmp + two_eta_cos);

        const FloatP tmp_f = eta2k2 + cosi * cosi;
        const FloatP Rperp2 = guarded_div(tmp_f - two_eta_cos, tmp_f + two_eta_cos);

        return (Rparl2 + Rperp2) * 0.5f;
    }

    FloatP FrFull(const FloatP &cosi, const FloatP &cost,
                  const FloatP &eta, const FloatP &k)
    {
        const FloatP eta2k2 = eta * eta + k * k;
        const FloatP two_cos_cos = (FloatP(2.f) * cosi) * cost;
        const FloatP common = two_cos_cos * eta;

        const FloatP tmp = eta2k2 * (cosi * cosi) + cost * cost;
        const FloatP Rparl2 = guarded_div(tmp - common, tmp + common);

        const FloatP tmp_f = eta2k2 * (cost * cost) + cosi * cosi;
        const FloatP Rperp2 = guarded_div(tmp_f - common, tmp_f + common);

        return (Rparl2 + Rperp2) * 0.5f;
    }

    FloatP FresnelApproxEta(const FloatP &Fr)
    {
        const FloatP one(1.f);
        const FloatP sqrtR = sqrt(clamp(Fr, FloatP(0.f), FloatP(0.999f)));
        return (one + sqrtR) / (one - sqrtR);
    }

    FloatP FresnelApproxK(const FloatP &Fr)
    {
        const FloatP one(1.f);
        const FloatP r = clamp(Fr, FloatP(0.f), FloatP(0.999f));
        return FloatP(2.f) * sqrt(r / (one - r));
    }

    FresnelGeneralP ResolveAuto(const FresnelGeneralP &fg)
    {
        if (fg.model != FresnelModel::Auto)
            return fg;
        FresnelGeneralP out = fg;
        out.model = ResolveModel(fg.eta, fg.k);
        return out;
    }

    FloatP FresnelGeneralEvaluate(const FresnelGeneralP &fg, const FloatP &cosi)
    {
        // lux FresnelGeneral::Evaluate: cosi > 0 means the ray arrives
        // entering; cosi < 0 is the exit.
        const FresnelModel model =
            fg.model == FresnelModel::Auto ? ResolveModel(fg.eta, fg.k) : fg.model;

        if (model == FresnelModel::Conductor)
            return select(cosi > FloatP(0.f), FrCond(cosi, fg.eta, fg.k),
                          FloatP(0.f));

        const FloatP one(1.f);
        // Snell: sin^2(t) = (1 - cos^2 i) / eta^2 entering, * eta^2 exiting.
        FloatP sint2 = max(one - cosi * cosi, FloatP(0.f));
        sint2 = select(cosi > FloatP(0.f), sint2 / (fg.eta * fg.eta),
                       sint2 * (fg.eta * fg.eta));
        sint2 = clamp(sint2, FloatP(0.f), one);
        const FloatP cost2 = one - sint2;

        if (model == FresnelModel::Dielectric)
        {
            // Entering uses eta, exiting inverts. At TIR sint2 clamps to 1
            // so cost -> 0 and FrDiel2 returns F = 1 to match lux.
            const FloatP cost = sqrt(cost2);
            const FloatP eta =
                select(cosi > FloatP(0.f), fg.eta, one / fg.eta);
            return FrDiel2(abs(cosi), cost, eta);
        }

        // Complex-IOR refraction where exiting applies the conjugate inverse
        // transform eta -> eta/d^2, k -> -k/d^2 with d^2 = eta^2 + k^2.
        const FloatP a = FloatP(2.f) * fg.k * fg.k * sint2;
        const FloatP cost = sqrt((cost2 + sqrt(cost2 * cost2 + a * a)) * FloatP(0.5f));
        const FloatP d2 = fg.eta * fg.eta + fg.k * fg.k;
        const FloatP etaExit = fg.eta / d2;
        const FloatP kExit = -fg.k / d2;
        const FloatP eta = select(cosi > FloatP(0.f), fg.eta, etaExit);
        const FloatP k = select(cosi > FloatP(0.f), fg.k, kExit);
        return FrFull(abs(cosi), cost, eta, k);
    }

} // namespace lux2
