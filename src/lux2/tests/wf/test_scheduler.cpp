/***************************************************************************
 *   This file is part of LuxRender.                                       *
 *                                                                         *
 *   To the extent possible under law, the author(s) have dedicated all    *
 *   copyright and related neighboring rights to the below code to the     *
 *   public domain worldwide. The below code is distributed without any    *
 *   warranty.                                                             *
 *                                                                         *
 *   See: <https://creativecommons.org/publicdomain/zero/1.0/>             *
 ***************************************************************************/

// Wavefront scheduler unit test (no rendering).
//
// Verifies the pure bookkeeping in wf/scheduler.h after the flatten-to-
// connection-items rewrite:
//   - MisBinTable: 2D (level(iMax), level(jMax)) key from the item's (t,s);
//   - ConnQueues: FIFO within a bin, dense top-up across neighbour bins;
//   - Scheduler: OnEyeDone/OnLightDone expand into (nE-1)*nL Gen items,
//     (nE-1) NEE items and nL Cam items, and the pending token balance
//     reaches exactly 0 once every queued item is completed.

#include "integrators/wf/scheduler.h"

#include <cstdint>
#include <iostream>
#include <vector>

using namespace lux2;
using namespace lux2::wf;

namespace
{
int g_failures = 0;

void Check(bool cond, const char *what)
{
    if (!cond)
    {
        std::cerr << "  FAIL: " << what << std::endl;
        ++g_failures;
    }
}

// ---- MisBinTable -------------------------------------------------------
void TestMisLevel()
{
    Check(MisLevel(0) == 0, "lvl(0)==0");
    Check(MisLevel(-3) == 0, "lvl(neg)==0");
    Check(MisLevel(1) == 1 && MisLevel(4) == 4, "lvl(1..4) exact");
    Check(MisLevel(5) == 5 && MisLevel(6) == 5, "lvl(5..6)==5");
    Check(MisLevel(7) == 6 && MisLevel(9) == 6, "lvl(7..9)==6");
    Check(MisLevel(10) == 7 && MisLevel(99) == 7, "lvl(>=10)==7");
}

void TestBinTable()
{
    // eyeDepth=lightDepth=16, minEye=1.
    MisBinTable bt;
    bt.Build(16, 16);
    // (t,s)=(1,1): iMax=min(0,..)=0, jMax=min(1,15)=1 -> (lvl0,lvl1).
    Check(bt.At(1, 1) == 0 * kMisLevels + 1, "bin(1,1)==(0,1)");
    // (t,s)=(8,4): iMax=min(7,12)=7->6, jMax=min(4,8)=4->4 -> 6*8+4.
    Check(bt.At(8, 4) == 6 * kMisLevels + 4, "bin(8,4)==(6,4)");
    // (t,s)=(2,1): iMax=min(1,15)=1->1, jMax=min(1,14)=1->1 -> 1*8+1.
    Check(bt.At(2, 1) == 1 * kMisLevels + 1, "bin(2,1)==(1,1)");
    // Neighbour list: bin's own key is first, distance ordering monotonic.
    const int b = bt.At(8, 4);
    Check(bt.neigh[b][0] == b, "neigh[b][0]==b");
    for (int k = 1; k < MisBinTable::kBins; ++k)
    {
        const int bi = b / kMisLevels, bj = b % kMisLevels;
        const int dx = std::abs(bt.neigh[b][k] / kMisLevels - bi) +
                       std::abs(bt.neigh[b][k] % kMisLevels - bj);
        const int dprev = std::abs(bt.neigh[b][k - 1] / kMisLevels - bi) +
                          std::abs(bt.neigh[b][k - 1] % kMisLevels - bj);
        Check(dx >= dprev, "neighbour distance non-decreasing");
    }
}

// ---- ConnQueues --------------------------------------------------------
void TestQueueFifoTopUp()
{
    MisBinTable bt;
    bt.Build(16, 16);
    ConnQueues q(&bt);

    // Push 3 items all at (t,s)=(8,4) -> same bin, FIFO order preserved.
    for (uint32_t i = 0; i < 3u; ++i)
        q.Push({10 + i, 20 + i, i, 0u, 8, 4});
    Check(q.Size() == 3u, "3 queued");
    Check(q.FullestBin() == bt.At(8, 4), "fullest bin is (8,4)'s");

    // One more item in a different bin. A no-top-up Fill takes only the
    // primary bin (3); a top-up Fill drains both into one packet (4).
    q.Push({99u, 98u, 7u, 0u, 1, 1});
    UInt32P ePath, lPath, sample, slot;
    Int32P t, s;
    const uint32_t pure =
        q.Fill(q.FullestBin(), ePath, lPath, sample, slot, t, s, false);
    Check(pure == 3u, "no-top-up Fill takes only the primary bin");
    alignas(64) uint32_t eA[PACKET_WIDTH];
    enoki::store_unaligned(eA, ePath);
    // The pure fill popped the three (8,4) items in push order.
    Check(eA[0] == 10u && eA[1] == 11u && eA[2] == 12u,
          "FIFO within primary bin");
    Check(q.Size() == 1u, "cross-bin item left after pure fill");
    const uint32_t take =
        q.Fill(q.FullestBin(), ePath, lPath, sample, slot, t, s, true);
    Check(take == 1u, "top-up Fill drains the remaining bin");
    Check(q.Size() == 0u, "queue drained");
    Check(q.FullestBin() == -1, "no fullest bin when empty");
}

// The O(1) fullest-bin bitmap must agree with a brute-force scan after every
// push/pop, including when the fullest bin drains and a lower level wins.
void TestFullestBinIncremental()
{
    MisBinTable bt;
    bt.Build(16, 16);
    ConnQueues q(&bt);

    auto brute = [&]()
    {
        int best = -1;
        uint32_t bestN = 0;
        for (int b = 0; b < ConnQueues::kBins; ++b)
            if (q.BinSize(b) > bestN)
            {
                bestN = q.BinSize(b);
                best = b;
            }
        return best;
    };

    // Three distinct bins with counts 5, 2, 4 (via distinct (t,s) keys).
    const int bA = bt.At(16, 16), bB = bt.At(2, 2), bC = bt.At(8, 1);
    for (int i = 0; i < 5; ++i)
        q.Push({uint32_t(i), 0u, 0u, 0u, 16, 16});
    for (int i = 0; i < 2; ++i)
        q.Push({uint32_t(i), 0u, 0u, 0u, 2, 2});
    for (int i = 0; i < 4; ++i)
        q.Push({uint32_t(i), 0u, 0u, 0u, 8, 1});
    Check(q.FullestBin() == bA && brute() == bA, "fullest is the 5-count bin");

    // Drain bA entirely (pure fill takes all 5); fullest must fall to bC (4).
    UInt32P ePath, lPath, sample, slot;
    Int32P t, s;
    q.Fill(bA, ePath, lPath, sample, slot, t, s, false);
    Check(q.FullestBin() == bC && q.FullestBin() == brute(),
          "fullest falls to 4-count after 5 drains");

    // Push a new max into bB; fullest must jump to it.
    for (int i = 0; i < 6; ++i)
        q.Push({uint32_t(i), 0u, 0u, 0u, 2, 2});
    Check(q.FullestBin() == bB && q.FullestBin() == brute(),
          "fullest jumps to new 8-count bin");

    // Drain everything via repeated top-up fills; fullest -> -1.
    while (!q.Empty())
        q.Fill(q.FullestBin(), ePath, lPath, sample, slot, t, s, true);
    Check(q.FullestBin() == -1, "empty -> fullest -1");
}

// Regression: a bin can hold far more than 64 items -- at eyeDepth=lightDepth=16
// a single pair queues (nE-1)*nL = 225 Gen items and the quantized MIS levels
// drop most of them into one bin. The occupancy tracking must stay correct for
// counts above the bin count (the old bitmap indexed a 64-bit mask and a
// kBins+1 array by item count).
void TestFullestBinOver64()
{
    MisBinTable bt;
    bt.Build(16, 16);
    ConnQueues q(&bt);

    auto brute = [&]()
    {
        int best = -1;
        uint32_t bestN = 0;
        for (int b = 0; b < ConnQueues::kBins; ++b)
            if (q.BinSize(b) > bestN)
            {
                bestN = q.BinSize(b);
                best = b;
            }
        return best;
    };

    const int big = bt.At(16, 16), small = bt.At(2, 2);
    constexpr int kMany = 200; // > kBins
    for (int i = 0; i < kMany; ++i)
        q.Push({uint32_t(i), 0u, 0u, 0u, 16, 16});
    for (int i = 0; i < 3; ++i)
        q.Push({uint32_t(i), 0u, 0u, 0u, 2, 2});

    Check(q.BinSize(big) == uint32_t(kMany), "over-capacity bin holds all items");
    Check(q.FullestBin() == big && q.FullestBin() == brute(),
          "fullest found with >64 items in one bin");

    // Drain the big bin past both 64 and its own count; fullest must fall to
    // the small bin only once the big one drops below it.
    UInt32P ePath, lPath, sample, slot;
    Int32P t, s;
    int rounds = 0;
    while (q.BinSize(big) > 3u && rounds < kMany)
    {
        q.Fill(q.FullestBin(), ePath, lPath, sample, slot, t, s, false);
        Check(q.FullestBin() == brute(), "fullest tracks scan while draining");
        ++rounds;
    }
    Check(q.FullestBin() == small && q.FullestBin() == brute(),
          "fullest falls to the remaining bin");

    while (!q.Empty())
        q.Fill(q.FullestBin(), ePath, lPath, sample, slot, t, s, true);
    Check(q.FullestBin() == -1, "empty -> fullest -1 after overflow");
}

// ---- Scheduler ---------------------------------------------------------
void TestEmissionEyeFirst()
{
    // One sample, nT=2. Eye drains first (nE=5), then two lights (nL=4,3).
    SampleTable tbl;
    tbl.Init(4, /*nT=*/2);
    PathStore eyePs, lightPs;
    eyePs.n.assign(64, 0u);
    lightPs.n.assign(64, 0u);
    MisBinTable bt;
    bt.Build(16, 16);
    Scheduler sched(tbl, eyePs, lightPs, bt);

    const uint32_t s = tbl.AcquireSlot();
    Check(s == 0u, "AcquireSlot returns slot 0 (LIFO head)");
    tbl.SetLightPath(s, 0, 7u);
    tbl.SetLightPath(s, 1, 8u);
    eyePs.n[5] = 5u;
    lightPs.n[7] = 4u;
    lightPs.n[8] = 3u;
    // Base pending: 1 NEE + nT Gen + 1 CamNee + nT Cam.
    tbl.pending[s] = 1u + 2u + (1u + 2u);

    // Eye done first: NEE expands to nE-1=4, CamNee=1, no Gen yet.
    sched.OnEyeDone(s, /*path=*/5);
    Check(sched.nee.Size() == 4u, "nee = nE-1 items on eye done");
    Check(sched.camNee.Size() == 1u, "camNee = 1 item");
    Check(sched.gen.Size() == 0u, "no gen before lights done");

    // Light j=0 (nL=4): cam += 4, gen += (nE-1)*nL = 4*4 = 16.
    sched.OnLightDone(s, /*path=*/7, /*j=*/0);
    Check(sched.cam.Size() == 4u, "cam = nL items on light done");
    Check(sched.gen.Size() == 16u, "gen = (nE-1)*nL after first light");

    // Light j=1 (nL=3): cam += 3, gen += 4*3 = 12.
    sched.OnLightDone(s, /*path=*/8, /*j=*/1);
    Check(sched.cam.Size() == 7u, "cam total = 4+3");
    Check(sched.gen.Size() == 28u, "gen total = 16+12");

    // Drain every queued item; pending must land exactly on 0.
    const uint32_t total = sched.nee.Size() + sched.camNee.Size() +
                           sched.gen.Size() + sched.cam.Size();
    for (uint32_t k = 0; k < total; ++k)
        sched.CompleteItem(s);
    Check(tbl.pending[s] == 0u, "pending balances to exactly 0");
    Check(!sched.retireQueue.empty(), "sample queued for retire");
}

void TestEmissionLightFirst()
{
    // Lights drain before the eye: no Gen until the eye completes.
    SampleTable tbl;
    tbl.Init(4, /*nT=*/1);
    PathStore eyePs, lightPs;
    eyePs.n.assign(64, 0u);
    lightPs.n.assign(64, 0u);
    MisBinTable bt;
    bt.Build(16, 16);
    Scheduler sched(tbl, eyePs, lightPs, bt);

    const uint32_t s = tbl.AcquireSlot();
    tbl.SetLightPath(s, 0, 9u);
    eyePs.n[6] = 5u;
    lightPs.n[9] = 4u;
    // Base pending: 1 NEE + nT Gen + 1 CamNee + nT Cam.
    tbl.pending[s] = 1u + 1u + (1u + 1u);

    sched.OnLightDone(s, /*path=*/9, /*j=*/0);
    Check(sched.cam.Size() == 4u, "cam = nL items even before eye done");
    Check(sched.gen.Size() == 0u, "no gen before eye done");
    Check(sched.nee.Empty(), "no nee before eye done");

    sched.OnEyeDone(s, /*path=*/6);
    Check(sched.nee.Size() == 4u, "nee = nE-1 when eye finally done");
    Check(sched.camNee.Size() == 1u, "camNee = 1 item");
    Check(sched.gen.Size() == (5u - 1u) * 4u, "gen emitted once eye catches up");

    const uint32_t total = sched.nee.Size() + sched.camNee.Size() +
                           sched.gen.Size() + sched.cam.Size();
    for (uint32_t k = 0; k < total; ++k)
        sched.CompleteItem(s);
    Check(tbl.pending[s] == 0u, "pending balances to exactly 0 (light-first)");
}

void TestEmptyCursorSkipped()
{
    // nE<2 or nL<1 -> no Gen items (would spin a connector).
    SampleTable tbl;
    tbl.Init(4, /*nT=*/2);
    PathStore eyePs, lightPs;
    eyePs.n.assign(64, 0u);
    lightPs.n.assign(64, 0u);
    MisBinTable bt;
    bt.Build(16, 16);
    Scheduler sched(tbl, eyePs, lightPs, bt);

    const uint32_t s = tbl.AcquireSlot();
    tbl.SetLightPath(s, 0, 1u); // nL=1 -> valid (nL>=1)
    tbl.SetLightPath(s, 1, 2u); // nL=0 -> invalid
    eyePs.n[3] = 1u;            // nE=1 -> invalid for Gen
    lightPs.n[1] = 1u;
    lightPs.n[2] = 0u;
    tbl.pending[s] = 1u + 2u + (1u + 2u);

    sched.OnEyeDone(s, /*path=*/3);
    sched.OnLightDone(s, 1u, 0);
    sched.OnLightDone(s, 2u, 1);
    Check(sched.gen.Size() == 0u, "nE<2 suppresses all gen items");

    // Now a valid eye (nE=3) with one nL=1 and one nL=0 light.
    SampleTable tbl2;
    tbl2.Init(4, 2);
    Scheduler sched2(tbl2, eyePs, lightPs, bt);
    const uint32_t s2 = tbl2.AcquireSlot();
    tbl2.SetLightPath(s2, 0, 1u); // nL=1 valid
    tbl2.SetLightPath(s2, 1, 2u); // nL=0 invalid
    eyePs.n[4] = 3u;
    tbl2.pending[s2] = 1u + 2u + (1u + 2u);
    sched2.OnEyeDone(s2, 4);
    sched2.OnLightDone(s2, 1u, 0);
    sched2.OnLightDone(s2, 2u, 1);
    // Only the nL>=1 light contributes: (nE-1)*nL = 2*1 = 2.
    Check(sched2.gen.Size() == 2u, "only the nL>=1 light emits gen items");
}

void TestCancelLightBalance()
{
    // A cancelled light entry settles its reserved Gen (+Cam) token now;
    // the sample must still reach pending==0 once the rest drains.
    SampleTable tbl;
    tbl.Init(4, /*nT=*/1);
    PathStore eyePs, lightPs;
    eyePs.n.assign(64, 0u);
    lightPs.n.assign(64, 0u);
    MisBinTable bt;
    bt.Build(16, 16);
    Scheduler sched(tbl, eyePs, lightPs, bt);

    const uint32_t s = tbl.AcquireSlot();
    eyePs.n[5] = 3u;
    tbl.pending[s] = 1u + 1u + (1u + 1u); // NEE + Gen + CamNee + Cam

    sched.OnEyeDone(s, 5);
    sched.CancelLight(s, 0); // settles the reserved Gen + Cam tokens
    Check(sched.gen.Size() == 0u, "cancel emits no gen items");
    Check(sched.cam.Size() == 0u, "cancel emits no cam items");

    const uint32_t total = sched.nee.Size() + sched.camNee.Size();
    for (uint32_t k = 0; k < total; ++k)
        sched.CompleteItem(s);
    Check(tbl.pending[s] == 0u, "cancelled sample still balances to 0");
}

void TestPendingComplete()
{
    SampleTable tbl;
    tbl.Init(4, 1);
    PathStore eyePs, lightPs;
    MisBinTable bt;
    bt.Build(16, 16);
    Scheduler sched(tbl, eyePs, lightPs, bt);
    const uint32_t s = tbl.AcquireSlot();
    tbl.pending[s] = 3u;
    Check(!sched.CompleteWork(s, 2u), "not complete at 1 owed");
    Check(sched.CompleteWork(s, 1u), "complete at 0 owed");
}

} // namespace

int main()
{
    std::cout << "lux2 wf test_scheduler (flattened connection items)"
              << std::endl;
    TestMisLevel();
    TestBinTable();
    TestQueueFifoTopUp();
    TestFullestBinIncremental();
    TestFullestBinOver64();
    TestEmissionEyeFirst();
    TestEmissionLightFirst();
    TestEmptyCursorSkipped();
    TestCancelLightBalance();
    TestPendingComplete();
    if (g_failures == 0)
        std::cout << "scheduler bookkeeping passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
