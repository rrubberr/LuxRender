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

// Wavelength mutation for ERPT chains.

#ifndef LUX2_INTEGRATORS_ERPT_LAMBDA_H
#define LUX2_INTEGRATORS_ERPT_LAMBDA_H

#include "integrators/erpt/mutator.h"

#include "core/color/color.h"

namespace lux2
{
    namespace erpt
    {
        class LambdaMutator final : public Mutator
        {
        public:
            MutationResult Propose(const WalkCtx &c, wf::PathStore &ps,
                                   const UInt32P &cur, const UInt32P &,
                                   const Int32P &n,
                                   const SpectrumWavelengthsP &,
                                   const MutRands &r,
                                   const MaskP &active) const override
            {
                MutationResult out;
                // Nothing written.
                out.spanLo = Int32P(1);
                out.spanHi = Int32P(0);
                out.winLo = Int32P(0);
                out.winHi = n - Int32P(1);
                out.firstEdge = Int32P(0);
                out.tRatio = FloatP(1.f); // uniform p(lambda)

                const FloatP wlNew =
                    FloatP(WAVELENGTH_START) +
                    (FloatP(WAVELENGTH_END) - FloatP(WAVELENGTH_START)) * r.u[0];
                SpectrumWavelengthsP swNew;
                swNew.FromWavelength(wlNew);

                // Full path at lambda.
                MaskP okF(false);
                const SWCSpectrumP fy = EvalCanonWindow(
                    c, ps, cur, n, Int32P(0), n - Int32P(1), Int32P(0), swNew,
                    /*traceVisibility=*/false, active, &okF);
                // Raw f.
                out.newWl = wlNew;
                out.fyScalar = fy;
                out.ok = active && okF && (out.fyScalar > FloatP(0.f));
                return out;
            }
        };

    } // namespace erpt
} // namespace lux2

#endif // LUX2_INTEGRATORS_ERPT_LAMBDA_H
