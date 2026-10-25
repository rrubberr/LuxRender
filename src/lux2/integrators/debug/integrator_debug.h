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

// Debug instrumentation shared by the integrators.

#ifndef LUX2_INTEGRATORS_INTEGRATOR_DEBUG_H
#define LUX2_INTEGRATORS_INTEGRATOR_DEBUG_H

#include "core/color/spectrum.h"
#include "core/math/vecp.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace lux2
{
    struct BSDFEvalP;

    // Optional WeightPath with per-term split of the MIS denominator.
    struct Breakdown
    {
        FloatP base, directOrigin, hitLightAlt, lightExt, eyeExt, directPath;
        MaskP okLight, okEye;
        Breakdown()
            : base(0.f), directOrigin(0.f), hitLightAlt(0.f),
              lightExt(0.f), eyeExt(0.f), directPath(0.f) {}
    };

#ifdef LUX2_INTEGRATOR_DEBUG

    // ==================================================================
    // Bidirectional diagnostics
    // ==================================================================

    // One (nE,nL) ghost connection cell.
    struct GhostCell
    {
        uint64_t delta = 0;
        uint64_t mixed = 0;
        double energy = 0.0;
    };

    // Diagnostic store flushed by End().
    struct BidirDiagStore
    {
        std::vector<std::vector<GhostCell>> ghost; // [nE][nL]
        // Total BidirRecordFirefly calls and the global max |Lc*w|.
        uint64_t recordCalls = 0;
        float maxLcw = 0.f;
        struct Firefly
        {
            float lcw = 0.f, lc = 0.f, w = 0.f, d2 = 0.f;
            int s = 0, t = 0, px = 0, py = 0;
            // WeightPath denominator split.
            float base = 0.f, directOrigin = 0.f, hitLightAlt = 0.f;
            float lightExt = 0.f, eyeExt = 0.f, directPath = 0.f;
            bool okLight = false, okEye = false;
        };
        // Min-heap on lcw.
        std::vector<Firefly> top;
        static constexpr size_t kTopN = 64;
        mutable std::mutex mtx;

        void Init(int eyeDepth, int lightDepth);
        // Caller holds mutex.
        void InsertFirefly(const Firefly &f);
    };

    // Diagnostic accumulator, merged into the store at tile end.
    struct BidirDiagLocal
    {
        std::vector<std::vector<GhostCell>> ghost;      // [nE][nL]
        std::vector<BidirDiagStore::Firefly> fireflies; // unsorted
        // Counts BidirRecordFirefly invocations and the largest |Lc*w|.
        uint64_t recordCalls = 0;
        float maxLcw = 0.f;
        void Init(int eyeDepth, int lightDepth);
        // Keep only the kTopN largest by lcw.
        void TrimFireflies();
    };

    // Verify that converting a uniform area light's solid angle pdf to the
    // emitter's area measure recovers the selection pdf. Log on mismatch.
    void BidirCheckAreaDensity(const char *what, const FloatP &areaPdf,
                               const FloatP &area, const FloatP &selPdf);

    // Tally lobes that report reflectance (f > 0) but have no finite
    // solid angle pdf ("ghost" connections).
    void BidirTallyGhost(BidirDiagLocal *d, const BSDFEvalP &ef,
                         MaskP evaluated, MaskP cont, const SWCSpectrumP &Lc,
                         const FloatP &w, const UInt32P &flags, int nE,
                         int nL);

    // Record contributions above the firefly threshold.
    void BidirRecordFirefly(BidirDiagLocal *dl, float threshold,
                            const FloatP &x, const FloatP &y, MaskP cont,
                            const SWCSpectrumP &Lc, const FloatP &w,
                            const FloatP &d2, int s, int t,
                            const Breakdown *bd);

    // Flush the store's tallies and firefly heap to the log.
    void BidirEndDump(const BidirDiagStore &store, float threshold);

    // Merge a tile's accumulator into the shared store under its mutex.
    void BidirMergeTile(BidirDiagStore &store, BidirDiagLocal &dl);

    // ==================================================================
    // Packet occupancy counters
    // ==================================================================

    // Reset this tile's accumulators.
    void OccupancyBegin();
    // Active lanes this iteration.
    void OccupancyStep(int liveLanes);
    // Print the tile summary.
    // tag distinguishes the integrator in the log line.
    void OccupancyTileEnd(const char *tag, int tileX0, int tileY0,
                          unsigned packetWidth);

#else // !LUX2_INTEGRATOR_DEBUG ------------------------------------------

    struct BidirDiagStore
    {
    };
    struct BidirDiagLocal
    {
    };

    inline void BidirCheckAreaDensity(const char *, const FloatP &,
                                      const FloatP &, const FloatP &)
    {
    }
    inline void BidirTallyGhost(BidirDiagLocal *, const BSDFEvalP &, MaskP,
                                MaskP, const SWCSpectrumP &, const FloatP &,
                                const UInt32P &, int, int)
    {
    }
    inline void BidirRecordFirefly(BidirDiagLocal *, float, const FloatP &,
                                   const FloatP &, MaskP, const SWCSpectrumP &,
                                   const FloatP &, const FloatP &, int, int,
                                   const Breakdown *)
    {
    }
    inline void BidirEndDump(const BidirDiagStore &, float) {}
    inline void BidirMergeTile(BidirDiagStore &, BidirDiagLocal &) {}

    inline void OccupancyBegin() {}
    inline void OccupancyStep(int) {}
    inline void OccupancyTileEnd(const char *, int, int, unsigned) {}

#endif

} // namespace lux2

#endif // LUX2_INTEGRATORS_INTEGRATOR_DEBUG_H
