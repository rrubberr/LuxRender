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

    // Optional WeightPath with term split of the MIS denominator.
    struct Breakdown
    {
        FloatP base, directOrigin, hitLightAlt, lightExt, eyeExt, directPath;
        MaskP okLight, okEye;
        Breakdown()
            : base(0.f), directOrigin(0.f), hitLightAlt(0.f),
              lightExt(0.f), eyeExt(0.f), directPath(0.f) {}
    };

#ifdef LUX2_INTEGRATOR_DEBUG

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
        // Min heap on lcw.
        std::vector<Firefly> top;
        static constexpr size_t kTopN = 64;
        mutable std::mutex mtx;

        void Init(int eyeDepth, int lightDepth);
        // Caller holds mutex.
        void InsertFirefly(const Firefly &f);
    };

    // Diagnostic accumulator.
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
    // emitter's area measure recovers the selection pdf.
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

    // Merge a tile's accumulator into the shared store.
    void BidirMergeTile(BidirDiagStore &store, BidirDiagLocal &dl);

    // Packet occupancy counters

    // Reset this tile's accumulators.
    void OccupancyBegin();
    // Active lanes this iteration.
    void OccupancyStep(int liveLanes);
    // Print the tile summary.
    // tag distinguishes the integrator in the log line.
    void OccupancyTileEnd(const char *tag, int tileX0, int tileY0,
                          unsigned packetWidth);

    // Wavefront connection statistics.
    enum WfLoadSite
    {
        WF_LOAD_GEN = 0, // GenConnector
        WF_LOAD_NEE,     // NeeConnector
        WF_LOAD_CAM,     // CamConnector
        WF_LOAD_CAMNEE,  // CamNeeConnector
        WF_LOAD_OTHER,   // consumers / anything else
        WF_LOAD_SITE_COUNT
    };

    // A Gen pair was admitted, histogram nE, nL and the product.
    void WfStatPair(uint32_t nE, uint32_t nL);
    // One WeightPathV call.
    void WfStatMis(int iMax, int jMax, int sumI = -1, int sumJ = -1,
                   int nAct = 0);
    // One LoadVertex from `site`, with `lanes` active lanes.
    void WfStatLoad(WfLoadSite site, int lanes);
    // One GenConnector::Step iteration with `liveLanes` alive.
    void WfStatGenStep(int liveLanes);
    // One completed Gen packet, record step count.
    void WfStatGenPacket(int steps);

    enum WfQueue
    {
        WF_Q_NEE = 0,
        WF_Q_CAMNEE,
        WF_Q_GEN,
        WF_Q_CAM,
        WF_Q_COUNT
    };
    enum WfWalker
    {
        WF_WALK_EYE = 0,
        WF_WALK_LIGHT,
        WF_WALK_COUNT
    };
    // One connector packet dispatched with `fill` lanes.
    void WfStatDispatch(WfQueue q, int fill, bool partial);
    // One walker step.
    void WfStatWalker(WfWalker w, int aliveLanes, int freeSlots);

    // Which loop step consumed an iteration.
    enum WfLoopStep
    {
        WF_LOOP_RETIRE = 0, // flushRetire ran
        WF_LOOP_DISPATCH,   // minFill dispatch
        WF_LOOP_WALKER,     // walker step at step 3
        WF_LOOP_FLUSH,      // stall flush at need=1
        WF_LOOP_STARVE,     // starvation-guard step
        WF_LOOP_COUNT
    };
    void WfStatLoopStep(WfLoopStep s);

    // Periodic slot census.
    void WfStatCensus(int freeSlots, int eyeWalk, int waitLight, int lightWalk,
                      int pendOnly, int retireQ,
                      int qNee, int qCamNee, int qGen, int qCam);
    // Lanes killed by a light refill that were midlight.
    void WfStatLightKill(int killedLanes);
    // Samples still pending when the tile loop exits.
    void WfStatLeakedSlots(int leaked);
    // One sample splatted at retire.
    void WfStatSplatted(int n);
    // One light refill attempt admitting `dead` lanes.
    void WfStatLightRefill(int dead);

    // Wall time phases.
    enum WfPhase
    {
        WF_PH_EYE_REFILL = 0, // PlanPixels + slot/path alloc + origin vertex
        WF_PH_EYE_ADV,        // trace + vertex write for one eye step
        WF_PH_LIGHT_REFILL,
        WF_PH_LIGHT_ADV,
        WF_PH_DONE,   // EyeDone/LightDone scheduler routing
        WF_PH_RETIRE, // flushRetire (splat + slot free)
        WF_PH_GEN,
        WF_PH_NEE,
        WF_PH_CAMNEE,
        WF_PH_CAM,
        WF_PH_SCHED, // dispatch checks + item completion (main loop)
        WF_PH_LOOP,  // whole tile loop wall time
        WF_PH_COUNT
    };
    void WfStatTimeNs(int phase, double ns);

    // Scoped accumulator.
    class WfTimer
    {
    public:
        explicit WfTimer(int phase);
        ~WfTimer();
        WfTimer(const WfTimer &) = delete;
        WfTimer &operator=(const WfTimer &) = delete;

    private:
        int m_phase;
        double m_t0;
    };
    // Print all tallies.
    void WfStatDump();

#else // !LUX2_INTEGRATOR_DEBUG

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

    enum WfLoadSite
    {
        WF_LOAD_GEN = 0,
        WF_LOAD_NEE,
        WF_LOAD_CAM,
        WF_LOAD_CAMNEE,
        WF_LOAD_OTHER,
        WF_LOAD_SITE_COUNT
    };
    inline void WfStatPair(uint32_t, uint32_t) {}
    inline void WfStatMis(int, int, int, int, int) {}
    inline void WfStatLoad(WfLoadSite, int) {}
    inline void WfStatGenStep(int) {}
    inline void WfStatGenPacket(int) {}
    enum WfQueue
    {
        WF_Q_NEE = 0,
        WF_Q_CAMNEE,
        WF_Q_GEN,
        WF_Q_CAM,
        WF_Q_COUNT
    };
    enum WfWalker
    {
        WF_WALK_EYE = 0,
        WF_WALK_LIGHT,
        WF_WALK_COUNT
    };
    inline void WfStatDispatch(WfQueue, int, bool) {}
    inline void WfStatWalker(WfWalker, int, int) {}
    enum WfLoopStep
    {
        WF_LOOP_RETIRE = 0,
        WF_LOOP_DISPATCH,
        WF_LOOP_WALKER,
        WF_LOOP_FLUSH,
        WF_LOOP_STARVE,
        WF_LOOP_COUNT
    };
    inline void WfStatLoopStep(WfLoopStep) {}
    inline void WfStatCensus(int, int, int, int, int, int, int, int, int, int) {}
    inline void WfStatLightKill(int) {}
    inline void WfStatLeakedSlots(int) {}
    inline void WfStatSplatted(int) {}
    inline void WfStatLightRefill(int) {}
    enum WfPhase
    {
        WF_PH_EYE_REFILL = 0,
        WF_PH_EYE_ADV,
        WF_PH_LIGHT_REFILL,
        WF_PH_LIGHT_ADV,
        WF_PH_DONE,
        WF_PH_RETIRE,
        WF_PH_GEN,
        WF_PH_NEE,
        WF_PH_CAMNEE,
        WF_PH_CAM,
        WF_PH_LOOP,
        WF_PH_COUNT
    };
    inline void WfStatTimeNs(int, double) {}
    class WfTimer
    {
    public:
        explicit WfTimer(int) {}
    };
    inline void WfStatDump() {}

#endif

} // namespace lux2

#endif // LUX2_INTEGRATORS_INTEGRATOR_DEBUG_H
