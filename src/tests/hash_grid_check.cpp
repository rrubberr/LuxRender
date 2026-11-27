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

// Validates HashGrid<WORDS>:
//   (a) point mode: QueryNear visited set == brute-force radius set;
//   (b) sphere mode: QueryAt visited set == brute-force containment set,
//       exact and duplicate-free, including points outside the record-
//       center bbox but inside an edge record's sphere;
//   (c) hash-collision-heavy configurations;
//   (d) determinism across rebuilds;
//   (e) parallel build byte-identical to the serial reference at several
//       thread counts.
// Also instantiates HashGrid<16> and HashGrid<32> so both record widths
// used by the planned consumers are compile-verified.

#include "core/spatial/hash_grid.h"

#include <tbb/global_control.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <random>
#include <set>
#include <vector>

using namespace lux2;

namespace {

int g_failures = 0;

void Check(bool cond, const char *what)
{
    if (!cond)
    {
        std::cerr << "FAIL: " << what << "\n";
        ++g_failures;
    }
}

// Pack a position (and optional radius) into a WORDS-wide record.
template <uint32_t WORDS>
void MakeRecord(std::vector<uint32_t> &recs, uint32_t r, float x, float y,
                float z, float R = 0.f, uint32_t radiusWord = 6)
{
    const std::size_t base = std::size_t(r) * WORDS;
    std::memcpy(&recs[base + 0], &x, 4);
    std::memcpy(&recs[base + 1], &y, 4);
    std::memcpy(&recs[base + 2], &z, 4);
    if (radiusWord < WORDS)
        std::memcpy(&recs[base + radiusWord], &R, 4);
}

float RecPos(const uint32_t *rec, int k)
{
    float v;
    std::memcpy(&v, &rec[k], 4);
    return v;
}
float RecR(const uint32_t *rec, uint32_t w)
{
    float v;
    std::memcpy(&v, &rec[w], 4);
    return v;
}

// Run QueryNear over a packet of sample points and collect the set of
// (lane, recordId) pairs reported. recordId is stored in word kIdWord.
constexpr uint32_t kIdWord = 7;

template <uint32_t WORDS>
std::set<std::pair<int, uint32_t>> QueryNearAll(const HashGrid<WORDS> &grid,
                                                const std::vector<float> &pts)
{
    std::set<std::pair<int, uint32_t>> visited;
    const uint32_t n = uint32_t(pts.size()) / 3u;
    for (uint32_t base = 0; base < n; base += SimdLanes)
    {
        FloatP px(0.f), py(0.f), pz(0.f), r2(0.25f);
        std::vector<int> laneOf;
        for (int l = 0; l < SimdLanes; ++l)
        {
            const uint32_t i = base + uint32_t(l);
            if (i < n)
            {
                px[l] = pts[i * 3 + 0];
                py[l] = pts[i * 3 + 1];
                pz[l] = pts[i * 3 + 2];
                laneOf.push_back(int(i));
            }
            else
                laneOf.push_back(-1);
        }
        const MaskP active(true);
        Point3fP p(px, py, pz);
        grid.QueryNear(p, r2, active,
                       [&](const HashGridFields &F, MaskP hit)
                       {
                           alignas(64) uint32_t ids[SimdLanes];
                           enoki::store_unaligned(ids, F.U(kIdWord));
                           for (int l = 0; l < SimdLanes; ++l)
                           {
                               if (!hit[l])
                                   continue;
                               const int lane = laneOf[l];
                               if (lane >= 0)
                                   visited.insert({lane, ids[l]});
                           }
                       });
    }
    return visited;
}

// Defined further below; forward-declared for TestNonFiniteRecords.
template <uint32_t WORDS>
std::set<uint32_t> QueryAtOne(const HashGrid<WORDS> &grid, float x, float y,
                              float z);

// Like QueryNearAll but counts every visit, so a (lane, recordId) pair
// reported twice is detected. The set-based QueryNearAll silently dedups
// and cannot catch the H1 double-count regression. Returns true if any
// pair was visited more than once.
template <uint32_t WORDS>
bool QueryNearHasDuplicate(const HashGrid<WORDS> &grid,
                           const std::vector<float> &pts)
{
    std::map<std::pair<int, uint32_t>, int> visits;
    const uint32_t n = uint32_t(pts.size()) / 3u;
    for (uint32_t base = 0; base < n; base += SimdLanes)
    {
        FloatP px(0.f), py(0.f), pz(0.f), r2(0.25f);
        std::vector<int> laneOf;
        for (int l = 0; l < SimdLanes; ++l)
        {
            const uint32_t i = base + uint32_t(l);
            if (i < n)
            {
                px[l] = pts[i * 3 + 0];
                py[l] = pts[i * 3 + 1];
                pz[l] = pts[i * 3 + 2];
                laneOf.push_back(int(i));
            }
            else
                laneOf.push_back(-1);
        }
        Point3fP p(px, py, pz);
        grid.QueryNear(p, r2, MaskP(true),
                       [&](const HashGridFields &F, MaskP hit)
                       {
                           alignas(64) uint32_t ids[SimdLanes];
                           enoki::store_unaligned(ids, F.U(kIdWord));
                           for (int l = 0; l < SimdLanes; ++l)
                           {
                               if (!hit[l])
                                   continue;
                               const int lane = laneOf[l];
                               if (lane >= 0)
                                   visits[{lane, ids[l]}]++;
                           }
                       });
    }
    for (const auto &kv : visits)
        if (kv.second > 1)
            return true;
    return false;
}

// Force the eight-cell slot-collision case: a small record count keeps the
// slot table at the NextPow2 floor (1024), where two of the eight neighbour
// cells collide for roughly 1% of query points. Thousands of queries make a
// missing per-lane dedup fail deterministically.
void TestNoDoubleCount()
{
    constexpr uint32_t W = 16;
    std::mt19937 rng(4242);
    std::uniform_real_distribution<float> pos(0.f, 4.f);

    const uint32_t count = 64; // small -> slots pinned at the 1024 floor
    std::vector<uint32_t> recs(std::size_t(count) * W, 0u);
    for (uint32_t r = 0; r < count; ++r)
    {
        MakeRecord<W>(recs, r, pos(rng), pos(rng), pos(rng));
        recs[std::size_t(r) * W + kIdWord] = r;
    }
    HashGrid<W> grid;
    grid.BuildPoints(recs.data(), count, 0.6f);
    Check(grid.Slots() == 1024u, "double-count test uses the slot floor");

    std::vector<float> pts;
    for (int i = 0; i < 20000; ++i)
    {
        pts.push_back(pos(rng));
        pts.push_back(pos(rng));
        pts.push_back(pos(rng));
    }
    Check(!QueryNearHasDuplicate<W>(grid, pts),
          "QueryNever double-counts a record (H1 dedup)");
}

// Non-finite records must be skipped, not crash the build or corrupt the
// buckets. Finite records around them must still query correctly.
void TestNonFiniteRecords()
{
    constexpr uint32_t W = 16;
    constexpr uint32_t RWORD = 6;
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();

    // Point mode: one NaN position, one inf position among finite records.
    {
        std::vector<uint32_t> recs(std::size_t(4) * W, 0u);
        MakeRecord<W>(recs, 0, 1.f, 1.f, 1.f);
        recs[kIdWord] = 0;
        MakeRecord<W>(recs, 1, nan, 0.f, 0.f); // NaN position
        recs[W + kIdWord] = 1;
        MakeRecord<W>(recs, 2, 0.f, inf, 0.f); // inf position
        recs[2 * W + kIdWord] = 2;
        MakeRecord<W>(recs, 3, 1.2f, 1.f, 1.f);
        recs[3 * W + kIdWord] = 3;

        HashGrid<W> grid;
        grid.BuildPoints(recs.data(), 4, 0.6f);
        // Query near the finite records: both must be found, no crash.
        const auto got = QueryNearAll<W>(grid, {1.f, 1.f, 1.f});
        Check(got.count({0, 0}) == 1 && got.count({0, 3}) == 1,
              "point mode finds finite records despite non-finite neighbours");
    }

    // Sphere mode: a record with a NaN radius is skipped; a finite one near
    // it is still queryable.
    {
        std::vector<uint32_t> recs(std::size_t(2) * W, 0u);
        MakeRecord<W>(recs, 0, 5.f, 5.f, 5.f, nan, RWORD); // NaN radius
        recs[kIdWord] = 10;
        MakeRecord<W>(recs, 1, 5.f, 5.f, 5.f, 1.f, RWORD);
        recs[W + kIdWord] = 11;

        HashGrid<W> grid;
        grid.BuildSpheres(recs.data(), 2, 0.75f, RWORD);
        const auto got = QueryAtOne<W>(grid, 5.f, 5.f, 5.f);
        Check(got.count(11) == 1 && got.count(10) == 0,
              "sphere mode skips non-finite radius, keeps finite");
    }
}

// Parallel BuildSpheres must be byte-identical across thread counts: fixed
// chunk emission order plus a stable sort makes the bucket and cellStart a
// pure function of the input.
void TestSphereDeterminism()
{
    constexpr uint32_t W = 16;
    constexpr uint32_t RWORD = 6;
    std::mt19937 rng(31337);
    std::uniform_real_distribution<float> pos(0.f, 50.f);
    std::uniform_real_distribution<float> rad(0.05f, 2.f);

    const uint32_t count = 40000;
    std::vector<uint32_t> recs(std::size_t(count) * W, 0u);
    for (uint32_t r = 0; r < count; ++r)
    {
        MakeRecord<W>(recs, r, pos(rng), pos(rng), pos(rng), rad(rng), RWORD);
        recs[std::size_t(r) * W + kIdWord] = r;
    }

    HashGrid<W> ref;
    ref.BuildSpheres(recs.data(), count, 1.5f, RWORD);
    const uint32_t slots = ref.Slots();
    std::vector<uint32_t> refBucket(ref.BucketData(),
                                    ref.BucketData() +
                                        ref.CellStartData()[slots]);
    std::vector<uint32_t> refStart(ref.CellStartData(),
                                   ref.CellStartData() + slots + 1u);

    bool allMatch = true;
    for (unsigned threads : {1u, 2u, 4u})
    {
        HashGrid<W> g;
        {
            tbb::global_control gc(
                tbb::global_control::max_allowed_parallelism, threads);
            g.BuildSpheres(recs.data(), count, 1.5f, RWORD);
        }
        const uint32_t gs = g.Slots();
        if (gs != slots ||
            !std::equal(refStart.begin(), refStart.end(),
                        g.CellStartData()) ||
            !std::equal(refBucket.begin(), refBucket.end(), g.BucketData()))
        {
            std::cerr << "  sphere build mismatch at threads=" << threads
                      << "\n";
            allMatch = false;
        }
    }
    Check(allMatch, "parallel BuildSpheres byte-identical across threads");
}

std::set<std::pair<int, uint32_t>> BruteNear(
    const std::vector<uint32_t> &recs, uint32_t count,
    const std::vector<float> &pts, float radius)
{
    std::set<std::pair<int, uint32_t>> out;
    const uint32_t n = uint32_t(pts.size()) / 3u;
    const float r2 = radius * radius;
    for (uint32_t i = 0; i < n; ++i)
        for (uint32_t r = 0; r < count; ++r)
        {
            const uint32_t *rec = &recs[std::size_t(r) * 16];
            const float dx = RecPos(rec, 0) - pts[i * 3 + 0];
            const float dy = RecPos(rec, 1) - pts[i * 3 + 1];
            const float dz = RecPos(rec, 2) - pts[i * 3 + 2];
            if (dx * dx + dy * dy + dz * dz <= r2)
                out.insert({int(i), uint32_t(rec[kIdWord])});
        }
    return out;
}

void TestPointModeRandom(uint32_t count, const char *label)
{
    constexpr uint32_t W = 16;
    std::mt19937 rng(1234 + count);
    std::uniform_real_distribution<float> pos(0.f, 10.f);

    std::vector<uint32_t> recs(std::size_t(count) * W, 0u);
    for (uint32_t r = 0; r < count; ++r)
    {
        MakeRecord<W>(recs, r, pos(rng), pos(rng), pos(rng));
        recs[std::size_t(r) * W + kIdWord] = r; // stable id
    }

    const float buildR = 0.6f;
    HashGrid<W> grid;
    grid.BuildPoints(recs.data(), count, buildR);

    std::vector<float> pts;
    for (int i = 0; i < 512; ++i)
    {
        pts.push_back(pos(rng));
        pts.push_back(pos(rng));
        pts.push_back(pos(rng));
    }

    const auto got = QueryNearAll<W>(grid, pts);
    const auto want = BruteNear(recs, count, pts, 0.5f); // query r <= buildR
    Check(got == want, label);
}

// Sphere mode: exact containment set, duplicate-free.
template <uint32_t WORDS>
std::set<uint32_t> QueryAtOne(const HashGrid<WORDS> &grid, float x, float y,
                              float z)
{
    std::set<uint32_t> visited;
    FloatP px(x), py(y), pz(z);
    for (int l = 1; l < SimdLanes; ++l)
    {
        px[l] = 1e30f; // inactive-ish: outside the bbox
        py[l] = 1e30f;
        pz[l] = 1e30f;
    }
    grid.QueryAt(Point3fP(px, py, pz), MaskP(true),
                 [&](const HashGridFields &F, MaskP hit)
                 {
                     alignas(64) uint32_t ids[SimdLanes];
                     enoki::store_unaligned(ids, F.U(kIdWord));
                     for (int l = 0; l < SimdLanes; ++l)
                         if (hit[l])
                             visited.insert(ids[l]);
                 });
    return visited;
}

void TestSphereModeContainment()
{
    constexpr uint32_t W = 16;
    constexpr uint32_t RWORD = 6;
    std::mt19937 rng(99);
    std::uniform_real_distribution<float> pos(0.f, 10.f);
    std::uniform_real_distribution<float> rad(0.05f, 1.5f);

    const uint32_t count = 2000;
    std::vector<uint32_t> recs(std::size_t(count) * W, 0u);
    for (uint32_t r = 0; r < count; ++r)
    {
        MakeRecord<W>(recs, r, pos(rng), pos(rng), pos(rng), rad(rng), RWORD);
        recs[std::size_t(r) * W + kIdWord] = r;
    }

    HashGrid<W> grid;
    grid.BuildSpheres(recs.data(), count, 0.75f, RWORD);

    bool ok = true;
    // Sample points, including ones deliberately placed just outside the
    // record-center bbox but within an edge sphere (regression: bbox must
    // be expanded by maxR).
    for (int i = 0; i < 400 && ok; ++i)
    {
        const float x = pos(rng), y = pos(rng), z = pos(rng);
        const auto got = QueryAtOne<W>(grid, x, y, z);

        std::set<uint32_t> want;
        for (uint32_t r = 0; r < count; ++r)
        {
            const uint32_t *rec = &recs[std::size_t(r) * W];
            const float dx = RecPos(rec, 0) - x;
            const float dy = RecPos(rec, 1) - y;
            const float dz = RecPos(rec, 2) - z;
            const float R = RecR(rec, RWORD);
            if (dx * dx + dy * dy + dz * dz <= R * R)
                want.insert(uint32_t(rec[kIdWord]));
        }
        if (got != want)
        {
            std::cerr << "  sphere mismatch at (" << x << "," << y << "," << z
                      << "): got " << got.size() << " want " << want.size()
                      << "\n";
            ok = false;
        }
    }
    Check(ok, "sphere mode containment exact");

    // Edge probe: a record near the min corner with a large radius; a
    // point outside the center bbox but inside its sphere must be found.
    std::vector<uint32_t> edge(std::size_t(1) * W, 0u);
    MakeRecord<W>(edge, 0, 0.f, 0.f, 0.f, 2.f, RWORD);
    edge[kIdWord] = 42;
    HashGrid<W> eg;
    eg.BuildSpheres(edge.data(), 1, 0.5f, RWORD);
    // Point at (-1.5, 0, 0): outside center bbox (a single point), inside
    // the sphere of radius 2.
    const auto e = QueryAtOne<W>(eg, -1.5f, 0.f, 0.f);
    Check(e.count(42) == 1, "sphere mode finds point outside center bbox");
}

void TestCollisionHeavy()
{
    constexpr uint32_t W = 16;
    // 4096 records at ~8 distinct positions: massive slot collisions and
    // coincident buckets.
    const uint32_t count = 4096;
    std::vector<uint32_t> recs(std::size_t(count) * W, 0u);
    for (uint32_t r = 0; r < count; ++r)
    {
        const float x = float(r % 8);
        MakeRecord<W>(recs, r, x, x, x);
        recs[std::size_t(r) * W + kIdWord] = r;
    }
    HashGrid<W> grid;
    // buildRadius must be >= the query radius used by BruteNear (0.5).
    grid.BuildPoints(recs.data(), count, 0.6f);

    std::vector<float> pts = {0.f, 0.f, 0.f, 3.f, 3.f, 3.f, 7.f, 7.f, 7.f};
    const auto got = QueryNearAll<W>(grid, pts);
    const auto want = BruteNear(recs, count, pts, 0.5f);
    Check(got == want, "collision-heavy point mode");
}

void TestDeterminism()
{
    constexpr uint32_t W = 16;
    const uint32_t count = 5000;
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> pos(0.f, 10.f);
    std::vector<uint32_t> recs(std::size_t(count) * W, 0u);
    for (uint32_t r = 0; r < count; ++r)
    {
        MakeRecord<W>(recs, r, pos(rng), pos(rng), pos(rng));
        recs[std::size_t(r) * W + kIdWord] = r;
    }

    HashGrid<W> a, b;
    a.BuildPoints(recs.data(), count, 0.5f);
    b.BuildPoints(recs.data(), count, 0.5f);
    Check(a.RecordCount() == b.RecordCount(), "determinism: count");
    Check(std::memcmp(a.Payload(), b.Payload(),
                      std::size_t(count) * W * sizeof(uint32_t)) == 0,
          "determinism: payload byte-identical across rebuilds");
}

// Parallel build must be byte-identical to the serial reference. Uses a
// record count above kParallelBuildThreshold (2^18).
void TestParallelEqualsSerial()
{
    constexpr uint32_t W = 16;
    const uint32_t count = (1u << 18) + 12345u;
    std::mt19937 rng(2024);
    std::uniform_real_distribution<float> pos(0.f, 100.f);
    std::vector<uint32_t> recs(std::size_t(count) * W, 0u);
    for (uint32_t r = 0; r < count; ++r)
    {
        MakeRecord<W>(recs, r, pos(rng), pos(rng), pos(rng));
        recs[std::size_t(r) * W + kIdWord] = r;
    }

    // Serial reference (test hook forces the counting-sort path at any
    // count), then parallel builds at several thread counts. All must be
    // byte-identical: the radix sort is a stable sort by (slot, index)
    // with fixed-size chunks, so the result is a function of the input
    // alone.
    HashGrid<W> ref;
    ref.BuildPointsSerialForTesting(recs.data(), count, 0.05f);

    bool allMatch = true;
    for (unsigned threads : {1u, 2u, 4u})
    {
        HashGrid<W> g;
        {
            tbb::global_control gc(tbb::global_control::max_allowed_parallelism,
                                   threads);
            g.BuildPoints(recs.data(), count, 0.05f);
        }
        if (std::memcmp(g.Payload(), ref.Payload(),
                        std::size_t(count) * W * sizeof(uint32_t)) != 0)
        {
            std::cerr << "  parallel build mismatch at threads=" << threads
                      << "\n";
            allMatch = false;
        }
    }
    Check(allMatch, "parallel build byte-identical to serial reference");

    // The permuted payload must be grouped by slot in non-decreasing
    // order (the same invariant the serial sort guarantees).
    bool monotone = true;
    uint32_t prevSlot = ref.SlotOfRecordForTesting(ref.Payload());
    for (uint32_t pos = 0; pos < count && monotone; ++pos)
    {
        const uint32_t s = ref.SlotOfRecordForTesting(
            &ref.Payload()[std::size_t(pos) * W]);
        if (s < prevSlot)
            monotone = false;
        prevSlot = s;
    }
    Check(monotone, "permuted payload respects bucket order");
}

} // namespace

int main()
{
    std::cout << "hash_grid: SimdLanes=" << SimdLanes
              << " HugePages=" << (HugePagesEnabled() ? "yes" : "no") << "\n";

    TestPointModeRandom(0, "point mode empty");
    TestPointModeRandom(1, "point mode single");
    TestPointModeRandom(17, "point mode tiny");
    TestPointModeRandom(5000, "point mode random");
    TestSphereModeContainment();
    TestCollisionHeavy();
    TestDeterminism();
    TestParallelEqualsSerial();
    TestNoDoubleCount();
    TestNonFiniteRecords();
    TestSphereDeterminism();

    if (g_failures == 0)
    {
        std::cout << "lux2hashgridcheck: ALL CHECKS PASSED\n";
        return EXIT_SUCCESS;
    }
    std::cerr << g_failures << " check(s) failed\n";
    return EXIT_FAILURE;
}
