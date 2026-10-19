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

// Shared packet occupancy counters for the wavefront integrators (path,
// bidirectional). Compiled only when LUX2_INTEGRATOR_DEBUG is defined
// (CMake option LUX2_INTEGRATOR_DEBUG); see integrator_debug.h.
//
// Accumulators are per-worker and reset at each tile begin, so a worker
// alternating integrators across tiles never mixes their tallies; the
// caller-supplied tag only selects the printed label.

#ifdef LUX2_INTEGRATOR_DEBUG

#include "integrators/integrator_debug.h"

#include <atomic>
#include <cstdio>

namespace lux2
{
    namespace
    {
        // Per worker occupancy accumulators.
        thread_local double g_occ_laneSum = 0.;
        thread_local double g_occ_iters = 0.;
        // Global cap on tile summaries to prevent log spam.
        std::atomic<long> g_occ_tileDumps{0};
        constexpr long OCC_MAX_DUMPS = 64;
    } // namespace

    void OccupancyBegin()
    {
        // The summary reflects this tile's paths only.
        g_occ_laneSum = 0.;
        g_occ_iters = 0.;
    }

    void OccupancyStep(int liveLanes)
    {
        g_occ_laneSum += double(liveLanes);
        g_occ_iters += 1.;
    }

    // Mean live lanes per iteration for this tile.
    void OccupancyTileEnd(const char *tag, int tileX0, int tileY0,
                          unsigned packetWidth)
    {
        if (g_occ_iters <= 0.)
            return;
        const long dumpIdx =
            g_occ_tileDumps.fetch_add(1, std::memory_order_relaxed);
        if (dumpIdx >= OCC_MAX_DUMPS)
            return;
        fprintf(stderr,
                "[OCCUPANCY %s tile(%d,%d)] meanLanes=%.2f/%u "
                "occupancy=%.1f%% iters=%.0f\n",
                tag, tileX0, tileY0, g_occ_laneSum / g_occ_iters, packetWidth,
                100. * (g_occ_laneSum / g_occ_iters) / double(packetWidth),
                g_occ_iters);
        if (dumpIdx == OCC_MAX_DUMPS - 1)
            fprintf(stderr,
                    "[OCCUPANCY] summary cap (%ld) reached; "
                    "suppressing further tile summaries\n",
                    OCC_MAX_DUMPS);
    }

} // namespace lux2

#endif // LUX2_INTEGRATOR_DEBUG
