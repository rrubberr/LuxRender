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

// Wavefront connection statistics.

#ifdef LUX2_INTEGRATOR_DEBUG

#include "integrators/debug/integrator_debug.h"

#include <atomic>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>

namespace lux2
{
    namespace
    {
        constexpr int DEPTH_BINS = 129; // nE/nL values 0..128
        constexpr int PROD_BINS = 10;   // log2 buckets of nE*nL
        constexpr int TRIP_BINS = 66;   // iMax/jMax 0..64
        constexpr int STEP_BINS = 130;  // steps per Gen packet
        constexpr int FILL_BINS = 65;   // lanes 0..63

        struct WfStats
        {
            // Pair admission histograms.
            std::atomic<uint64_t> nEHist[DEPTH_BINS];
            std::atomic<uint64_t> nLHist[DEPTH_BINS];
            std::atomic<uint64_t> prodHist[PROD_BINS];
            std::atomic<uint64_t> pairs{0};

            // WeightPathV trip counts.
            std::atomic<uint64_t> iMaxHist[TRIP_BINS];
            std::atomic<uint64_t> jMaxHist[TRIP_BINS];
            std::atomic<uint64_t> misCalls{0};
            std::atomic<uint64_t> misIterSum{0}; // sum(iMax + jMax)

            // LoadVertex attribution.
            std::atomic<uint64_t> loadCalls[WF_LOAD_SITE_COUNT];
            std::atomic<uint64_t> loadLanes[WF_LOAD_SITE_COUNT];

            // Gen stepping.
            std::atomic<uint64_t> genSteps{0};
            std::atomic<uint64_t> genStepLaneSum{0};
            std::atomic<uint64_t> genPackets{0};
            std::atomic<uint64_t> genPacketSteps[STEP_BINS];

            // Connector dispatch fill.
            std::atomic<uint64_t> dispPackets[WF_Q_COUNT];
            std::atomic<uint64_t> dispLanes[WF_Q_COUNT];
            std::atomic<uint64_t> dispPartial[WF_Q_COUNT];
            std::atomic<uint64_t> dispFill[WF_Q_COUNT][FILL_BINS];

            // Walker occupancy.
            std::atomic<uint64_t> walkSteps[WF_WALK_COUNT];
            std::atomic<uint64_t> walkAlive[WF_WALK_COUNT];
            std::atomic<uint64_t> walkFreeSum[WF_WALK_COUNT];
            std::atomic<uint64_t> walkNoFree[WF_WALK_COUNT];
            std::atomic<uint64_t> walkAliveHist[WF_WALK_COUNT][FILL_BINS];

            // Iteration attribution.
            std::atomic<uint64_t> loopSteps[WF_LOOP_COUNT];

            // Slot census.
            std::atomic<uint64_t> censuses{0};
            std::atomic<uint64_t> cenFreeSum{0};
            std::atomic<uint64_t> cenEyeWalk{0};   // paths done, pending
            std::atomic<uint64_t> cenWaitLight{0}; // eyeDone, light not spawned
            std::atomic<uint64_t> cenLightWalk{0}; // light path in flight
            std::atomic<uint64_t> cenPendOnly{0};  // all done, pending > 0
            std::atomic<uint64_t> cenRetireQ{0};
            std::atomic<uint64_t> cenQNee{0}, cenQCamNee{0}, cenQGen{0},
                cenQCam{0};

            // Light refill kills of midflight lanes.
            std::atomic<uint64_t> lightKilledLanes{0};
            std::atomic<uint64_t> leakedSlots{0};

            // Conservation.
            std::atomic<uint64_t> splatted{0};

            // Joint (nE,nL) pair histogram.
            static constexpr int JOINT = 16; // values >=16 share the last bin
            std::atomic<uint64_t> joint[JOINT][JOINT];

            // Dead lanes admitted per light refill attempt.
            std::atomic<uint64_t> lightRefills{0};
            std::atomic<uint64_t> lightRefillLanes{0};
            std::atomic<uint64_t> lightRefillHist[FILL_BINS];

            // Wall time per phase (ns).
            std::atomic<uint64_t> phaseNs[WF_PH_COUNT];

            // MIS live lane totals.
            std::atomic<uint64_t> misSumI{0}, misSumJ{0}, misAct{0};

            static int FillBin(int v)
            {
                return v < 0 ? 0 : (v < FILL_BINS ? v : FILL_BINS - 1);
            }

            static int DepthBin(int v)
            {
                return v < 0 ? 0 : (v < DEPTH_BINS ? v : DEPTH_BINS - 1);
            }
            static int TripBin(int v)
            {
                return v < 0 ? 0 : (v < TRIP_BINS ? v : TRIP_BINS - 1);
            }
            // log2 buckets: 0 -> 0, 1 -> 1, 2-3 -> 2, 4-7 -> 3, ...
            static int ProdBin(uint64_t p)
            {
                int b = 1;
                uint64_t lim = 1;
                while (p > lim && b < PROD_BINS - 1)
                {
                    ++b;
                    lim <<= 1;
                }
                return b;
            }
        };

        WfStats &Stats()
        {
            static WfStats s;
            return s;
        }

        void DumpAtExit() { WfStatDump(); }

        const char *LoadSiteName(WfLoadSite s)
        {
            switch (s)
            {
            case WF_LOAD_GEN:
                return "Gen";
            case WF_LOAD_NEE:
                return "NEE";
            case WF_LOAD_CAM:
                return "Cam";
            case WF_LOAD_CAMNEE:
                return "CamNee";
            default:
                return "Other";
            }
        }

        // Print a histogram as value:count pairs.
        void PrintHist(const char *name, const std::atomic<uint64_t> *h,
                       int bins)
        {
            fprintf(stderr, "[WFSTAT] %s:", name);
            bool any = false;
            for (int i = 0; i < bins; ++i)
            {
                const uint64_t n = h[i].load(std::memory_order_relaxed);
                if (n)
                {
                    fprintf(stderr, " %d:%" PRIu64, i, n);
                    any = true;
                }
            }
            if (!any)
                fprintf(stderr, " (empty)");
            fprintf(stderr, "\n");
        }
    } // namespace

    void WfStatPair(uint32_t nE, uint32_t nL)
    {
        WfStats &s = Stats();
        const int je = nE < 15u ? int(nE) : 15;
        const int jl = nL < 15u ? int(nL) : 15;
        s.joint[je][jl].fetch_add(1, std::memory_order_relaxed);
        s.nEHist[WfStats::DepthBin(int(nE))].fetch_add(1,
                                                       std::memory_order_relaxed);
        s.nLHist[WfStats::DepthBin(int(nL))].fetch_add(1,
                                                       std::memory_order_relaxed);
        s.prodHist[WfStats::ProdBin(uint64_t(nE) * uint64_t(nL))]
            .fetch_add(1, std::memory_order_relaxed);
        s.pairs.fetch_add(1, std::memory_order_relaxed);
    }

    void WfStatMis(int iMax, int jMax, int sumI, int sumJ, int nAct)
    {
        WfStats &s = Stats();
        s.iMaxHist[WfStats::TripBin(iMax)].fetch_add(1,
                                                     std::memory_order_relaxed);
        s.jMaxHist[WfStats::TripBin(jMax)].fetch_add(1,
                                                     std::memory_order_relaxed);
        s.misCalls.fetch_add(1, std::memory_order_relaxed);
        s.misIterSum.fetch_add(uint64_t(iMax > 0 ? iMax : 0) +
                                   uint64_t(jMax > 0 ? jMax : 0),
                               std::memory_order_relaxed);
        if (sumI >= 0 && nAct > 0)
        {
            s.misSumI.fetch_add(uint64_t(sumI), std::memory_order_relaxed);
            s.misSumJ.fetch_add(uint64_t(sumJ), std::memory_order_relaxed);
            s.misAct.fetch_add(uint64_t(nAct), std::memory_order_relaxed);
        }
    }

    namespace
    {
        double NowNs()
        {
            return std::chrono::duration<double, std::nano>(
                       std::chrono::steady_clock::now().time_since_epoch())
                .count();
        }
    } // namespace

    void WfStatTimeNs(int phase, double ns)
    {
        Stats().phaseNs[phase].fetch_add(uint64_t(ns),
                                         std::memory_order_relaxed);
    }

    WfTimer::WfTimer(int phase) : m_phase(phase), m_t0(NowNs()) {}
    WfTimer::~WfTimer() { WfStatTimeNs(m_phase, NowNs() - m_t0); }

    void WfStatLoad(WfLoadSite site, int lanes)
    {
        WfStats &s = Stats();
        s.loadCalls[site].fetch_add(1, std::memory_order_relaxed);
        s.loadLanes[site].fetch_add(uint64_t(lanes > 0 ? lanes : 0),
                                    std::memory_order_relaxed);
    }

    void WfStatGenStep(int liveLanes)
    {
        WfStats &s = Stats();
        s.genSteps.fetch_add(1, std::memory_order_relaxed);
        s.genStepLaneSum.fetch_add(uint64_t(liveLanes > 0 ? liveLanes : 0),
                                   std::memory_order_relaxed);
    }

    void WfStatGenPacket(int steps)
    {
        WfStats &s = Stats();
        s.genPackets.fetch_add(1, std::memory_order_relaxed);
        s.genPacketSteps[WfStats::TripBin(steps)]
            .fetch_add(1, std::memory_order_relaxed);
    }

    void WfStatDispatch(WfQueue q, int fill, bool partial)
    {
        WfStats &s = Stats();
        s.dispPackets[q].fetch_add(1, std::memory_order_relaxed);
        s.dispLanes[q].fetch_add(uint64_t(fill > 0 ? fill : 0),
                                 std::memory_order_relaxed);
        if (partial)
            s.dispPartial[q].fetch_add(1, std::memory_order_relaxed);
        s.dispFill[q][WfStats::FillBin(fill)].fetch_add(1,
                                                        std::memory_order_relaxed);
    }

    void WfStatWalker(WfWalker w, int aliveLanes, int freeSlots)
    {
        WfStats &s = Stats();
        s.walkSteps[w].fetch_add(1, std::memory_order_relaxed);
        s.walkAlive[w].fetch_add(uint64_t(aliveLanes > 0 ? aliveLanes : 0),
                                 std::memory_order_relaxed);
        s.walkFreeSum[w].fetch_add(uint64_t(freeSlots > 0 ? freeSlots : 0),
                                   std::memory_order_relaxed);
        if (freeSlots <= 0)
            s.walkNoFree[w].fetch_add(1, std::memory_order_relaxed);
        s.walkAliveHist[w][WfStats::FillBin(aliveLanes)].fetch_add(
            1, std::memory_order_relaxed);
    }

    void WfStatLoopStep(WfLoopStep s)
    {
        Stats().loopSteps[s].fetch_add(1, std::memory_order_relaxed);
    }

    void WfStatCensus(int freeSlots, int eyeWalk, int waitLight, int lightWalk,
                      int pendOnly, int retireQ,
                      int qNee, int qCamNee, int qGen, int qCam)
    {
        WfStats &s = Stats();
        s.censuses.fetch_add(1, std::memory_order_relaxed);
        s.cenFreeSum.fetch_add(uint64_t(freeSlots > 0 ? freeSlots : 0),
                               std::memory_order_relaxed);
        s.cenEyeWalk.fetch_add(uint64_t(eyeWalk > 0 ? eyeWalk : 0),
                               std::memory_order_relaxed);
        s.cenWaitLight.fetch_add(uint64_t(waitLight > 0 ? waitLight : 0),
                                 std::memory_order_relaxed);
        s.cenLightWalk.fetch_add(uint64_t(lightWalk > 0 ? lightWalk : 0),
                                 std::memory_order_relaxed);
        s.cenPendOnly.fetch_add(uint64_t(pendOnly > 0 ? pendOnly : 0),
                                std::memory_order_relaxed);
        s.cenRetireQ.fetch_add(uint64_t(retireQ > 0 ? retireQ : 0),
                               std::memory_order_relaxed);
        s.cenQNee.fetch_add(uint64_t(qNee > 0 ? qNee : 0),
                            std::memory_order_relaxed);
        s.cenQCamNee.fetch_add(uint64_t(qCamNee > 0 ? qCamNee : 0),
                               std::memory_order_relaxed);
        s.cenQGen.fetch_add(uint64_t(qGen > 0 ? qGen : 0),
                            std::memory_order_relaxed);
        s.cenQCam.fetch_add(uint64_t(qCam > 0 ? qCam : 0),
                            std::memory_order_relaxed);
    }

    void WfStatLightKill(int killedLanes)
    {
        if (killedLanes > 0)
            Stats().lightKilledLanes.fetch_add(uint64_t(killedLanes),
                                               std::memory_order_relaxed);
    }

    void WfStatLeakedSlots(int leaked)
    {
        if (leaked > 0)
            Stats().leakedSlots.fetch_add(uint64_t(leaked),
                                          std::memory_order_relaxed);
    }

    void WfStatSplatted(int n)
    {
        if (n > 0)
            Stats().splatted.fetch_add(uint64_t(n), std::memory_order_relaxed);
    }

    void WfStatLightRefill(int dead)
    {
        WfStats &s = Stats();
        s.lightRefills.fetch_add(1, std::memory_order_relaxed);
        if (dead > 0)
        {
            s.lightRefillLanes.fetch_add(uint64_t(dead),
                                         std::memory_order_relaxed);
            s.lightRefillHist[WfStats::FillBin(dead)]
                .fetch_add(1, std::memory_order_relaxed);
        }
    }

    void WfStatDump()
    {
        static bool dumped = false;
        if (dumped)
            return;
        dumped = true;
        WfStats &s = Stats();
        fprintf(stderr, "[WFSTAT] ===== wavefront connection stats =====\n");
        fprintf(stderr, "[WFSTAT] genPairs=%" PRIu64 " misCalls=%" PRIu64 " misIters/call=%.2f\n",
                s.pairs.load(std::memory_order_relaxed),
                s.misCalls.load(std::memory_order_relaxed),
                double(s.misIterSum.load(std::memory_order_relaxed)) /
                    double(s.misCalls.load(std::memory_order_relaxed) + 1));

        {
            // MIS trip-count spread: how far the per-call max sits above
            // the live-lane mean. Large spread = lanes ragged in depth.
            const uint64_t na = s.misAct.load(std::memory_order_relaxed);
            if (na > 0)
            {
                const double mi =
                    double(s.misSumI.load(std::memory_order_relaxed)) / double(na);
                const double mj =
                    double(s.misSumJ.load(std::memory_order_relaxed)) / double(na);
                fprintf(stderr,
                        "[WFSTAT] MIS live-lane mean i=%.2f j=%.2f "
                        "(max-vs-mean waste ~%.0f%%)\n",
                        mi, mj,
                        100.0 * (double(s.misIterSum.load(std::memory_order_relaxed)) / double(2.0 * s.misCalls.load(std::memory_order_relaxed) + 1) - (mi + mj) / 2.0) /
                            ((mi + mj) / 2.0 + 1e-9));
            }
        }

        PrintHist("nE hist", s.nEHist, DEPTH_BINS);
        PrintHist("nL hist", s.nLHist, DEPTH_BINS);
        PrintHist("nE*nL hist (log2 buckets)", s.prodHist, PROD_BINS);
        PrintHist("WeightPathV iMax hist", s.iMaxHist, TRIP_BINS);
        PrintHist("WeightPathV jMax hist", s.jMaxHist, TRIP_BINS);

        for (int i = 0; i < WF_LOAD_SITE_COUNT; ++i)
        {
            const uint64_t calls = s.loadCalls[i].load(std::memory_order_relaxed);
            const uint64_t lanes = s.loadLanes[i].load(std::memory_order_relaxed);
            fprintf(stderr,
                    "[WFSTAT] LoadVertex[%s] calls=%" PRIu64
                    " lanes=%" PRIu64 " meanLanes=%.2f\n",
                    LoadSiteName(WfLoadSite(i)), calls, lanes,
                    double(lanes) / double(calls + 1));
        }

        const uint64_t steps = s.genSteps.load(std::memory_order_relaxed);
        const uint64_t laneSum = s.genStepLaneSum.load(std::memory_order_relaxed);
        fprintf(stderr,
                "[WFSTAT] genSteps=%" PRIu64 " genPackets=%" PRIu64
                " steps/packet=%.2f meanLiveLanes=%.2f\n",
                steps, s.genPackets.load(std::memory_order_relaxed),
                double(steps) / double(s.genPackets.load(std::memory_order_relaxed) + 1),
                double(laneSum) / double(steps + 1));
        PrintHist("gen steps/packet hist", s.genPacketSteps, STEP_BINS);

        static const char *qn[WF_Q_COUNT] = {"NEE", "CamNee", "Gen", "Cam"};
        for (int q = 0; q < WF_Q_COUNT; ++q)
        {
            const uint64_t pk = s.dispPackets[q].load(std::memory_order_relaxed);
            const uint64_t ln = s.dispLanes[q].load(std::memory_order_relaxed);
            const uint64_t pa = s.dispPartial[q].load(std::memory_order_relaxed);
            fprintf(stderr,
                    "[WFSTAT] dispatch[%s] packets=%" PRIu64 " meanFill=%.2f "
                    "partial=%.1f%%\n",
                    qn[q], pk, double(ln) / double(pk + 1),
                    100.0 * double(pa) / double(pk + 1));
            PrintHist("  fill hist", s.dispFill[q], FILL_BINS);
        }
        static const char *wn[WF_WALK_COUNT] = {"Eye", "Light"};
        for (int w = 0; w < WF_WALK_COUNT; ++w)
        {
            const uint64_t st = s.walkSteps[w].load(std::memory_order_relaxed);
            fprintf(stderr,
                    "[WFSTAT] walker[%s] steps=%" PRIu64 " meanAlive=%.2f "
                    "meanFreeSlots=%.1f noFreeSlot=%.1f%%\n",
                    wn[w], st,
                    double(s.walkAlive[w].load(std::memory_order_relaxed)) /
                        double(st + 1),
                    double(s.walkFreeSum[w].load(std::memory_order_relaxed)) /
                        double(st + 1),
                    100.0 *
                        double(s.walkNoFree[w].load(std::memory_order_relaxed)) /
                        double(st + 1));
            PrintHist("  alive hist", s.walkAliveHist[w], FILL_BINS);
        }

        const uint64_t totLoop =
            s.loopSteps[WF_LOOP_RETIRE].load(std::memory_order_relaxed) +
            s.loopSteps[WF_LOOP_DISPATCH].load(std::memory_order_relaxed) +
            s.loopSteps[WF_LOOP_WALKER].load(std::memory_order_relaxed) +
            s.loopSteps[WF_LOOP_FLUSH].load(std::memory_order_relaxed) +
            s.loopSteps[WF_LOOP_STARVE].load(std::memory_order_relaxed);
        static const char *ls[WF_LOOP_COUNT] = {
            "retire", "dispatch", "walker", "stall-flush", "starve-walk"};
        fprintf(stderr, "[WFSTAT] loop iterations=%" PRIu64 ":", totLoop);
        for (int i = 0; i < WF_LOOP_COUNT; ++i)
            fprintf(stderr, " %s=%.1f%%", ls[i],
                    100.0 * double(s.loopSteps[i].load(std::memory_order_relaxed)) /
                        double(totLoop + 1));
        fprintf(stderr, "\n");

        const uint64_t nc = s.censuses.load(std::memory_order_relaxed) + 1;
        fprintf(stderr,
                "[WFSTAT] census n=%" PRIu64
                " free=%.1f eyeWalk=%.1f waitLight=%.1f lightWalk=%.1f"
                " pendOnly=%.1f retireQ=%.1f | qNee=%.1f qCamNee=%.1f"
                " qGen=%.1f qCam=%.1f\n",
                s.censuses.load(std::memory_order_relaxed),
                double(s.cenFreeSum.load(std::memory_order_relaxed)) / nc,
                double(s.cenEyeWalk.load(std::memory_order_relaxed)) / nc,
                double(s.cenWaitLight.load(std::memory_order_relaxed)) / nc,
                double(s.cenLightWalk.load(std::memory_order_relaxed)) / nc,
                double(s.cenPendOnly.load(std::memory_order_relaxed)) / nc,
                double(s.cenRetireQ.load(std::memory_order_relaxed)) / nc,
                double(s.cenQNee.load(std::memory_order_relaxed)) / nc,
                double(s.cenQCamNee.load(std::memory_order_relaxed)) / nc,
                double(s.cenQGen.load(std::memory_order_relaxed)) / nc,
                double(s.cenQCam.load(std::memory_order_relaxed)) / nc);
        fprintf(stderr,
                "[WFSTAT] lightKilledLanes=%" PRIu64 " leakedSlots=%" PRIu64
                " (both must be 0) splatted=%" PRIu64 "\n",
                s.lightKilledLanes.load(std::memory_order_relaxed),
                s.leakedSlots.load(std::memory_order_relaxed),
                s.splatted.load(std::memory_order_relaxed));

        const uint64_t lr = s.lightRefills.load(std::memory_order_relaxed) + 1;
        fprintf(stderr,
                "[WFSTAT] lightRefills=%" PRIu64 " meanDeadLanes=%.2f\n",
                s.lightRefills.load(std::memory_order_relaxed),
                double(s.lightRefillLanes.load(std::memory_order_relaxed)) / lr);
        PrintHist("  refill dead-lane hist", s.lightRefillHist, FILL_BINS);

        // Joint (nE,nL): rows nE, cols nL. Cost key is (nE-1)*nL steps.
        fprintf(stderr, "[WFSTAT] joint nE x nL (steps=(nE-1)*nL):\n");
        for (int i = 0; i < WfStats::JOINT; ++i)
        {
            bool any = false;
            for (int j = 0; j < WfStats::JOINT; ++j)
                if (s.joint[i][j].load(std::memory_order_relaxed))
                    any = true;
            if (!any)
                continue;
            fprintf(stderr, "[WFSTAT]   nE=%2d:", i);
            for (int j = 0; j < WfStats::JOINT; ++j)
            {
                const uint64_t n = s.joint[i][j].load(std::memory_order_relaxed);
                if (n)
                    fprintf(stderr, " %d:%" PRIu64, j, n);
            }
            fprintf(stderr, "\n");
        }

        // Wall time.
        static const char *ph[WF_PH_COUNT] = {
            "eyeRefill", "eyeAdvance", "lightRefill", "lightAdvance",
            "doneRoute", "retire", "Gen", "NEE",
            "CamNee", "Cam", "SCHED", "LOOP(total)"};
        double phaseSum = 0.0, loopNs = 0.0;
        for (int i = 0; i < WF_PH_COUNT; ++i)
        {
            const double v = double(s.phaseNs[i].load(std::memory_order_relaxed));
            if (i == WF_PH_LOOP)
                loopNs = v;
            else
                phaseSum += v;
        }
        fprintf(stderr, "[WFSTAT] phase time (sum=%.2fs):", phaseSum / 1e9);
        for (int i = 0; i < WF_PH_COUNT; ++i)
        {
            const double v = double(s.phaseNs[i].load(std::memory_order_relaxed));
            if (i == WF_PH_LOOP)
                continue;
            fprintf(stderr, " %s=%.1f%%", ph[i], 100.0 * v / (phaseSum + 1.0));
        }
        fprintf(stderr, "\n[WFSTAT] loop wall=%.2fs (unaccounted=%.1f%% of loop)\n",
                loopNs / 1e9,
                loopNs > 0.0 ? 100.0 * (loopNs - phaseSum) / loopNs : 0.0);
    }

    namespace
    {
        // Register the exit dump.
        struct WfStatAtExit
        {
            WfStatAtExit() { atexit(DumpAtExit); }
        };
        const WfStatAtExit g_wfStatAtExit;
    } // namespace

} // namespace lux2

#endif // LUX2_INTEGRATOR_DEBUG
