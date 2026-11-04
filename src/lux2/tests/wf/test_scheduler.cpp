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

// Phase D scheduler unit test (no rendering).
//
// Verifies the pure bookkeeping in wf/scheduler.h:
//   - PairQueues::ClassOf binning boundaries;
//   - PairQueues::FillPacket pops LIFO and masks the tail;
//   - Scheduler::OnEyeDone / OnLightDone emit NEE/CamNee/Cam and Gen pairs in
//     the right order regardless of which path drains first, and skip pairs
//     with an empty cursor space (nE<2 or nL<1).
//
// The stores are used only for their scalar n[] side table; no arena or
// records are touched.

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

// ---- PairQueues --------------------------------------------------------
void TestClassOf()
{
    Check(PairQueues::ClassOf(1, 1) == 0, "cls(1,1)==bin0");
    Check(PairQueues::ClassOf(8, 3) == 0, "cls(8,3)==bin0 (<=8)");
    Check(PairQueues::ClassOf(9, 1) == 1, "cls(9,1)==bin1 (<=16)");
    Check(PairQueues::ClassOf(16, 16) == 1, "cls(16,16)==bin1");
    Check(PairQueues::ClassOf(17, 2) == 2, "cls(17,2)==bin2 (<=32)");
    Check(PairQueues::ClassOf(33, 5) == 3, "cls(33,5)==bin3 (<=64)");
    Check(PairQueues::ClassOf(65, 1) == 4, "cls(65,1)==bin4 (<=128)");
    Check(PairQueues::ClassOf(128, 128) == 4, "cls(128,128)==bin4");
}

void TestFillPacket()
{
    PairQueues pq;
    // Push 3 pairs into bin 0.
    for (uint32_t i = 0; i < 3u; ++i)
        pq.Push({10 + i, 20 + i, i, 0u}, 4, 4);
    Check(pq.Queued() == 3u, "3 queued");
    Check(pq.FullestBin() == 0, "fullest bin is 0");

    UInt32P ePath, lPath, sample, slot;
    const uint32_t take = pq.FillPacket(0, ePath, lPath, sample, slot);
    Check(take == 3u, "FillPacket returns 3");
    alignas(64) uint32_t eA[PACKET_WIDTH], sA[PACKET_WIDTH];
    enoki::store_unaligned(eA, ePath);
    enoki::store_unaligned(sA, sample);
    // LIFO: last pushed (i=2) pops first.
    Check(eA[0] == 12u && eA[1] == 11u && eA[2] == 10u, "LIFO pop order");
    Check(sA[0] == 2u && sA[1] == 1u && sA[2] == 0u, "sample LIFO order");
    Check(pq.Queued() == 0u, "queue drained");
    Check(pq.FullestBin() == -1, "no full bin when empty");
}

// ---- Scheduler ---------------------------------------------------------
// A minimal store stand-in: only n[] is read by the scheduler.
struct FakeStore
{
    std::vector<uint32_t> n;
};

void TestEmissionEyeFirst()
{
    // One sample, nT=2. Eye drains first (nE=5), then two lights (nL=4,3).
    SampleTable tbl;
    tbl.Init(4, /*nT=*/2);
    PathStore eyePs, lightPs; // default-constructed: only .n used below
    eyePs.n.assign(64, 0u);
    lightPs.n.assign(64, 0u);

    Scheduler sched(tbl, eyePs, lightPs, /*useT1=*/true);

    // Init pushes Ns-1..0, so the LIFO free list pops slot 0 first.
    const uint32_t s = tbl.AcquireSlot();
    Check(s == 0u, "AcquireSlot returns slot 0 (LIFO head)");
    // Reserve two light list entries.
    tbl.SetLightPath(s, 0, 7u);
    tbl.SetLightPath(s, 1, 8u);
    eyePs.n[5] = 5u;
    lightPs.n[7] = 4u;
    lightPs.n[8] = 3u;

    // Eye done first: NEE + CamNee pushed, no Gen yet (no light done).
    sched.OnEyeDone(s, /*path=*/5);
    Check(sched.nee.Size() == 1u, "nee pushed on eye done");
    Check(sched.camNee.Size() == 1u, "camNee pushed on eye done (useT1)");
    Check(sched.gen.Queued() == 0u, "no gen pairs before lights done");

    // Light j=0 done: cam pushed, one gen pair (nE=5,nL=4 -> bin2? max=5 ->bin0).
    sched.OnLightDone(s, /*path=*/7, /*j=*/0);
    Check(sched.cam.Size() == 1u, "cam pushed on light done");
    Check(sched.gen.Queued() == 1u, "one gen pair after first light done");
    Check(PairQueues::ClassOf(5, 4) == 0, "pair (5,4) in bin0");

    // Light j=1 done: another gen pair.
    sched.OnLightDone(s, /*path=*/8, /*j=*/1);
    Check(sched.cam.Size() == 2u, "second cam pushed");
    Check(sched.gen.Queued() == 2u, "two gen pairs after both lights done");
}

void TestEmissionLightFirst()
{
    // Lights drain before the eye: no Gen until the eye completes.
    SampleTable tbl;
    tbl.Init(4, /*nT=*/1);
    PathStore eyePs, lightPs;
    eyePs.n.assign(64, 0u);
    lightPs.n.assign(64, 0u);
    Scheduler sched(tbl, eyePs, lightPs, /*useT1=*/false);

    const uint32_t s = tbl.AcquireSlot();
    tbl.SetLightPath(s, 0, 9u);
    eyePs.n[6] = 5u;
    lightPs.n[9] = 4u;

    sched.OnLightDone(s, /*path=*/9, /*j=*/0);
    Check(sched.cam.Empty(), "no cam when useT1 off");
    Check(sched.gen.Queued() == 0u, "no gen pair before eye done");
    Check(sched.nee.Empty(), "no nee before eye done");

    sched.OnEyeDone(s, /*path=*/6);
    Check(sched.nee.Size() == 1u, "nee pushed when eye finally done");
    Check(sched.gen.Queued() == 1u, "gen pair emitted once eye catches up");
}

void TestEmptyCursorSkipped()
{
    // nE<2 or nL<1 -> no Gen pair (would spin a connector).
    SampleTable tbl;
    tbl.Init(4, /*nT=*/2);
    PathStore eyePs, lightPs;
    eyePs.n.assign(64, 0u);
    lightPs.n.assign(64, 0u);
    Scheduler sched(tbl, eyePs, lightPs, false);

    const uint32_t s = tbl.AcquireSlot();
    tbl.SetLightPath(s, 0, 1u); // nL=1 -> valid (nL>=1)
    tbl.SetLightPath(s, 1, 2u); // nL=0 -> invalid
    eyePs.n[3] = 1u;            // nE=1 -> invalid for Gen
    lightPs.n[1] = 1u;
    lightPs.n[2] = 0u;

    sched.OnEyeDone(s, /*path=*/3);
    sched.OnLightDone(s, 1u, 0);
    sched.OnLightDone(s, 2u, 1);
    Check(sched.gen.Queued() == 0u, "nE<2 suppresses all gen pairs");

    // Now a valid eye (nE=3) with one nL=1 and one nL=0 light.
    SampleTable tbl2;
    tbl2.Init(4, 2);
    Scheduler sched2(tbl2, eyePs, lightPs, false);
    const uint32_t s2 = tbl2.AcquireSlot();
    tbl2.SetLightPath(s2, 0, 1u); // nL=1 valid
    tbl2.SetLightPath(s2, 1, 2u); // nL=0 invalid
    eyePs.n[4] = 3u;
    sched2.OnEyeDone(s2, 4);
    sched2.OnLightDone(s2, 1u, 0);
    sched2.OnLightDone(s2, 2u, 1);
    Check(sched2.gen.Queued() == 1u, "only the nL>=1 light pairs");
}

void TestPendingComplete()
{
    SampleTable tbl;
    tbl.Init(4, 1);
    PathStore eyePs, lightPs;
    Scheduler sched(tbl, eyePs, lightPs, false);
    const uint32_t s = tbl.AcquireSlot();
    tbl.pending[s] = 3u;
    Check(!sched.CompleteWork(s, 2u), "not complete at 1 owed");
    Check(sched.CompleteWork(s, 1u), "complete at 0 owed");
}

} // namespace

int main()
{
    std::cout << "lux2 wf test_scheduler (Phase D bookkeeping)" << std::endl;
    TestClassOf();
    TestFillPacket();
    TestEmissionEyeFirst();
    TestEmissionLightFirst();
    TestEmptyCursorSkipped();
    TestPendingComplete();
    if (g_failures == 0)
        std::cout << "scheduler bookkeeping passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
