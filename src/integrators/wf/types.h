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

// Shared wavefront types.

#ifndef LUX2_INTEGRATORS_WF_TYPES_H
#define LUX2_INTEGRATORS_WF_TYPES_H

#include "integrators/debug/integrator_debug.h"
#include "integrators/lane/tile_ctx.h"

#include <cstdint>

namespace lux2
{
    // Diagnostic configuration.
    struct DiagConfig
    {
        bool noMis = false;    // w = 1 (isolates each strategy's Lc)
        float misPower = 2.f;  // exponent p in the power heuristic (0,1,2)
        bool diag = false;     // enable DiagStore tallies and firefly recorder
        float threshold = 0.f; // firefly record threshold (0 = off)
        // Reorder connection packets by path id for arena locality.
        bool wfSortByPath = false;
    };

    namespace wf
    {
        struct SeedSink;
    }

    // Bidirectional's estimator state and debug accumulator.
    struct WalkCtx : TileCtx
    {
        const Camera *camera = nullptr;
        int eyeDepth = 8, lightDepth = 8;
        FloatP eyeRR, lightRR; // eyeThreshold, lightThreshold
        UInt32P cameraMediumID = UInt32P(LUX2_MEDIUM_CLEAR);
        // Verify that uniform area conversions of a solid angle
        // pdf recover the selection pdf.
        bool checkDensity = false;
        // Diagnostic accumulator.
        BidirDiagLocal *diag = nullptr;
        DiagConfig cfg;
        // Null in bidirectional. ERPT installs a sink that captures every
        // finished connection as a chain seed or a direct deposit.
        wf::SeedSink *seedSink = nullptr;
    };

    // Transient terms.
    struct Transient
    {
        FloatP eAF_last;            // eye.dARWeight[nE-1]
        FloatP eAR_last, eAR_prev;  // eye.dAWeight[nE-1], [nE-2]
        FloatP eRR_last, eRRR_last; // eye.rr/rrR[nE-1]
        FloatP lAR_last, lAR_prev;  // light.dARWeight[nL-1], [nL-2]
        FloatP lRR_last, lRRR_last; // light.rr/rrR[nL-1]
        FloatP pdfLightDirect;      // area measure including selection
        bool isLightDirect = false;
        // Connection vertices are non-specular.

        Transient()
            : eAF_last(0.f), eAR_last(0.f), eAR_prev(0.f), eRR_last(1.f),
              eRRR_last(1.f), lAR_last(0.f), lAR_prev(0.f), lRR_last(1.f),
              lRRR_last(1.f), pdfLightDirect(0.f) {}
    };

    // Copy the join-vertex densities out of a Transient for firefly logging.
    inline TransientSnap MakeTransientSnap(const Transient &tr)
    {
        TransientSnap s;
        s.eAF_last = tr.eAF_last;
        s.eAR_last = tr.eAR_last;
        s.eAR_prev = tr.eAR_prev;
        s.lAR_last = tr.lAR_last;
        s.lAR_prev = tr.lAR_prev;
        s.pdfLightDirect = tr.pdfLightDirect;
        return s;
    }

} // namespace lux2

#endif // LUX2_INTEGRATORS_WF_TYPES_H
