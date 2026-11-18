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

// Equal deposition chain logic (Cline et al. 2005).

#ifndef LUX2_INTEGRATORS_ERPT_CHAIN_H
#define LUX2_INTEGRATORS_ERPT_CHAIN_H

#include "integrators/erpt/erpt_math.h"

#include <cstdint>

namespace lux2
{
    namespace erpt
    {
        struct ChainParams
        {
            uint32_t m = 100;         // mutations per chain
            float ed = 0.f;           // deposition energy
            uint32_t maxConsec = 0;   // 0 = consecutive-sample filter off
            bool splitDeposit = true; // see header
            uint32_t maxChainsPerSeed = 1u << 16;
            // Share of steps that are wavelength mutations.
            float pLambda = 0.f;
        };

        // Where a deposit lands.
        struct Pixel
        {
            int32_t x = 0, y = 0;
            bool operator==(const Pixel &o) const { return x == o.x && y == o.y; }
        };

        // Run length tracker for the consecutive sample filter.
        struct ConsecFilter
        {
            Pixel last{-1, -1};
            uint32_t run = 0;

            // Call once per step with the pixel the chain is on.
            bool AdmitStay(const Pixel &state, uint32_t maxConsec)
            {
                if (state == last)
                    ++run;
                else
                {
                    last = state;
                    run = 1u;
                }
                return maxConsec == 0u || run <= maxConsec;
            }
            void Reset(const Pixel &start)
            {
                last = start;
                run = 0u;
            }
        };

        struct StepIn
        {
            bool valid = false; // a proposal was constructed and f(y) > 0
            float a = 0.f;      // acceptance probability (ignored if !valid)
            Pixel x;            // current state pixel
            Pixel y;            // proposal pixel (ignored if !valid)
            float u = 0.f;      // uniform [0,1) for the accept decision
        };

        struct StepOut
        {
            bool accept = false;
            // Up to two deposits per step in energy units.
            Pixel depPx[2];
            float depAmount[2] = {0.f, 0.f};
            int nDep = 0;
            // The proposal pixel counts as a "proposed mutation into pixel"
            // for the smoothing filter.
            bool proposedInto = false;
            Pixel proposedPx;
        };

        // One chain step.
        inline StepOut StepChain(const ChainParams &P, ConsecFilter &cf,
                                 const StepIn &in)
        {
            StepOut o;
            const float a = in.valid ? in.a : 0.f;
            o.accept = in.valid && (in.u < a);
            if (in.valid)
            {
                o.proposedInto = true;
                o.proposedPx = in.y;
            }

            if (P.splitDeposit)
            {
                if (a > 0.f)
                {
                    o.depPx[o.nDep] = in.y;
                    o.depAmount[o.nDep++] = P.ed * a;
                }
                const bool admit = cf.AdmitStay(in.x, P.maxConsec);
                if (admit && a < 1.f)
                {
                    o.depPx[o.nDep] = in.x;
                    o.depAmount[o.nDep++] = P.ed * (1.f - a);
                }
            }
            else
            {
                // Deposit e_d at the state after the step.
                const Pixel after = o.accept ? in.y : in.x;
                const bool admit = cf.AdmitStay(after, P.maxConsec);
                if (admit)
                {
                    o.depPx[o.nDep] = after;
                    o.depAmount[o.nDep++] = P.ed;
                }
            }
            return o;
        }

        // Counters.
        struct ChainStats
        {
            uint64_t seeds = 0, chains = 0, steps = 0;
            uint64_t proposed = 0, valid = 0, accepted = 0;
            uint64_t chainClamped = 0; // NumChains hit maxChainsPerSeed (biased!)
            uint64_t fxZero = 0;       // valid step with f(x) <= 0
            uint64_t seedGateFail = 0; // seed whose chain-start f(x) <= 0
            double energyIn = 0.0;     // sum of seed energies drained
            double energyOut = 0.0;    // sum of amounts deposited
            void Add(const ChainStats &o)
            {
                seeds += o.seeds;
                chains += o.chains;
                steps += o.steps;
                proposed += o.proposed;
                valid += o.valid;
                accepted += o.accepted;
                chainClamped += o.chainClamped;
                fxZero += o.fxZero;
                seedGateFail += o.seedGateFail;
                energyIn += o.energyIn;
                energyOut += o.energyOut;
            }
        };

    } // namespace erpt
} // namespace lux2

#endif // LUX2_INTEGRATORS_ERPT_CHAIN_H
