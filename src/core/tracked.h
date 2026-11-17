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

#ifndef LUX2_CORE_TRACKED_H
#define LUX2_CORE_TRACKED_H

#include "core/medium.h"
#include "core/math/math.h"
#include "core/rng.h"
#include "core/color/spectrum.h"

#include <enoki/array.h>

namespace lux2
{
    // ---------------------------------------------------------------------------
    // TrackedMedium
    // ---------------------------------------------------------------------------

    class TrackedMedium : public Medium
    {
    public:
        // Flight kills the lane, transmittance returns the accumulated beta.
        static constexpr int kMaxTrackIters = 1024;

        // Ratio tracking RR threshold.
        static constexpr float kRRThreshold = 0.1f;

        // Schlick k at a scatter.
        virtual FloatP PhaseK(const SpectrumWavelengthsP &sw,
                              const Point3fP &p,
                              const FloatP &u,
                              MaskP active) const = 0;

        void SampleInteraction(const SpectrumWavelengthsP &sw, const RayP &ray,
                               const FloatP &t0, const FloatP &t1,
                               const FloatP &u_flight, RNGP &rng,
                               MediumSampleP *out,
                               MaskP active) const override
        {
            Walk<true>(*this, sw, ray, t0, t1, u_flight, rng, out, active);
        }

        SWCSpectrumP Transmittance(const SpectrumWavelengthsP &sw,
                                   const RayP &ray, const FloatP &t0,
                                   const FloatP &t1, RNGP &rng,
                                   MaskP active) const override
        {
            MediumSampleP ms;
            ClearMediumSample(&ms, t1);
            Walk<false>(*this, sw, ray, t0, t1, FloatP(0.f), rng, &ms, active);
            return select(active, ms.weight, SWCSpectrumP(1.f));
        }

    private:
        // Flight: at each collision the lane scatters with probability
        //   sigma_s/sigma_bar (weight 1), is absorbed with probability
        //   sigma_a/sigma_bar (weight 0), or continues as a null collision.
        //   Emission Lve/sigma_bar accumulates at every collision.
        // Transmittance: ratio tracking beta *= 1 - sigma_t/sigma_bar with
        //   Russian roulette below kRRThreshold.
        template <bool Flight>
        static void Walk(const TrackedMedium &m, const SpectrumWavelengthsP &sw,
                         const RayP &ray, const FloatP &t0, const FloatP &t1,
                         const FloatP &u0, RNGP &rng, MediumSampleP *out,
                         MaskP active)
        {
            if (none(active))
                return;

            MajorantState ms;
            m.MajorantBegin(ray, t0, t1, ms, active);

            FloatP t = ms.tStart;
            SWCSpectrumP beta(1.f);
            SWCSpectrumP emit(0.f);
            // Flight uses the sampler's u_flight for its first step.
            FloatP u = Flight ? u0 : rng.NextFloat(active);
            MaskP live = active;
            MaskP scat(false);
            MaskP absorbed(false);
            FloatP tScat(t0), kScat(0.f);
            Point3fP pScat(0.f);
            SWCSpectrumP ssAt(0.f), stAt(0.f);

            for (int it = 0; it < kMaxTrackIters && any(live); ++it)
            {
                // Fetch the next segment when the current one is exhausted.
                const MaskP need = live && (t >= ms.tEnd);
                if (any(need))
                    live &= !(need && !m.MajorantNext(ms, need));
                if (none(live))
                    break;

                // Step Exp(sigma_bar).
                const FloatP uSafe =
                    enoki::min(enoki::max(u, FloatP(0.f)),
                               FloatP(1.f) - FloatP(1e-6f));
                const FloatP step =
                    select(ms.sigBar > FloatP(0.f),
                           -enoki::log(FloatP(1.f) - uSafe) /
                               enoki::max(ms.sigBar, FloatP(EPS_DENOM)),
                           FloatP(std::numeric_limits<float>::infinity()));
                const FloatP tn = t + step;

                // Past the segment end, clamp and fetch next
                // iteration.
                const MaskP crossed = live && (tn >= ms.tEnd);
                t = select(crossed, ms.tEnd, select(live, tn, t));
                const MaskP coll = live && !crossed;
                if (none(coll))
                {
                    u = rng.NextFloat(live);
                    continue;
                }

                MediumCoeffs c;
                m.Coeffs(sw, ray(t), coll, &c);
                const SWCSpectrumP sigT = c.sa + c.ss;

#ifdef LUX2_MEDIUM_CAP_COUNTERS
                // A conservative majorant requires sigma_t(sw) <= sigma_bar at
                // every collision.
                {
                    const MaskP viol = coll && (sigT > ms.sigBar);
                    g_trackMajorantViolations += enoki::count(viol);
                }
#endif

                // Emission at each collision.
                emit = select(coll, emit + beta * (c.Le / ms.sigBar), emit);

                if constexpr (Flight)
                {
                    const FloatP uc = rng.NextFloat(coll);
                    const MaskP real = coll && (uc < sigT / ms.sigBar);
                    const MaskP isScat = real && (uc < c.ss / ms.sigBar);
                    const MaskP isAbs = real && !isScat;

                    // Absorbed weight 0, path ends. Scatter weight 1.
                    beta = select(isAbs, SWCSpectrumP(0.f), beta);
                    absorbed = absorbed | isAbs;
                    scat = scat | isScat;
                    tScat = select(isScat, t, tScat);
                    // Same point handed to Coeffs above, so Contains() at the
                    // event agrees with the coefficients.
                    pScat = select(isScat, ray(t), pScat);
                    ssAt = select(isScat, c.ss, ssAt);
                    stAt = select(isScat, sigT, stAt);
                    if (any(isScat))
                    {
                        const FloatP uk = rng.NextFloat(isScat);
                        kScat = select(isScat, m.PhaseK(sw, ray(t), uk, isScat),
                                       kScat);
                    }
                    live &= !real;
                }
                else
                {
                    // Ratio tracking.
                    beta = select(coll, beta * (FloatP(1.f) - sigT / ms.sigBar),
                                  beta);

                    // Russian roulette.
                    const MaskP rr = coll && (beta < SWCSpectrumP(kRRThreshold));
                    if (any(rr))
                    {
                        const FloatP q = enoki::min(
                            beta / FloatP(kRRThreshold), FloatP(1.f));
                        const FloatP urr = rng.NextFloat(rr);
                        const MaskP kill = rr && (urr > q);
#ifdef LUX2_MEDIUM_CAP_COUNTERS
                        g_trackRRKills += enoki::count(kill);
#endif
                        beta = select(kill, SWCSpectrumP(0.f), beta);
                        beta = select(rr && !kill,
                                      beta / enoki::max(q, FloatP(EPS_DENOM)),
                                      beta);
                    }
                    live &= !(beta <= SWCSpectrumP(0.f));
                }

                u = rng.NextFloat(live);
            }

#ifdef LUX2_MEDIUM_CAP_COUNTERS
            // Lanes still alive at this point burned the whole budget.
            if constexpr (Flight)
                g_trackCapFlight += enoki::count(live);
            else
                g_trackCapTransmit += enoki::count(live);
#endif

            // A flight lane that exhausted the budget saw no event.
            if constexpr (Flight)
            {
                beta = select(live, SWCSpectrumP(0.f), beta);
                absorbed = absorbed || live;
            }

            // MediumSampleP::d is the distance from t0.
            const FloatP dEnd = select(scat, tScat, t) - t0;
            enoki::masked(out->d, active) = dEnd;
            enoki::masked(out->p, active) = pScat;
            enoki::masked(out->scattered, active) = scat && active;
            enoki::masked(out->absorbed, active) = absorbed && active;
            enoki::masked(out->weight, active) = beta;
            enoki::masked(out->emission, active) = emit;
            enoki::masked(out->sigma_s, active) = ssAt;
            enoki::masked(out->sigma_t, active) = stAt;
            enoki::masked(out->k, active) = kScat;
        }

#ifdef LUX2_MEDIUM_CAP_COUNTERS
    public:
        // Debug counters.
        static thread_local long g_trackCapFlight;
        static thread_local long g_trackCapTransmit;
        static thread_local long g_trackRRKills;
        // sigma_t(sw) exceeded the per-segment majorant sigma_bar at a
        // collision, the bound is not conservative.
        static thread_local long g_trackMajorantViolations;
#endif
    };

#ifdef LUX2_MEDIUM_CAP_COUNTERS
    inline thread_local long TrackedMedium::g_trackCapFlight = 0;
    inline thread_local long TrackedMedium::g_trackCapTransmit = 0;
    inline thread_local long TrackedMedium::g_trackRRKills = 0;
    inline thread_local long TrackedMedium::g_trackMajorantViolations = 0;
#endif

} // namespace lux2

#endif // LUX2_CORE_TRACKED_H
