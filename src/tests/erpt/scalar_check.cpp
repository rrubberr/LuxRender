// Unit checks for the dependency-free ERPT containers:
// SeedPool, SparseAccum, FilterGrid/BoxBlur/ApplyProposalFilter.
//   g++ -std=c++20 -O2 -fsanitize=address,undefined -I<src root> scalar_check.cpp
#include "integrators/erpt/seed_pool.h"
#include "integrators/erpt/smooth.h"
#include "integrators/erpt/sparse_accum.h"
#include "integrators/erpt/erpt_math.h"

#include <cmath>
#include <cstdio>
#include <array>
#include <map>
#include <random>
#include <utility>

using namespace lux2;
using namespace lux2::erpt;
static int g_fail = 0;
#define EXPECT(c) do { if (!(c)) { std::printf("  FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

static void TestPool()
{
    std::printf("SeedPool\n");
    SeedPool pool(32u, 4096);
    EXPECT(pool.Empty() && !pool.ShouldDrain());
    // variable-length records keep their payload and do not alias
    for (uint32_t i = 0; i < 5; ++i)
    {
        SeedHeader h; h.n = 3 + i; h.e = 1.f + i; h.seedKey = 100 + i;
        uint32_t *w = pool.Begin(h);
        for (uint32_t k = 0; k < h.n * 32u; ++k) w[k] = i * 100000u + k;
    }
    EXPECT(pool.Count() == 5);
    for (uint32_t i = 0; i < 5; ++i)
    {
        EXPECT(pool.Header(i).seedKey == 100 + i && pool.Header(i).n == 3 + i);
        const uint32_t *w = pool.Verts(i);
        bool ok = true;
        for (uint32_t k = 0; k < pool.Header(i).n * 32u; ++k) ok &= (w[k] == i * 100000u + k);
        EXPECT(ok);
    }
    // Byte accounting is by content, and the drain flag flips at the limit.
    const size_t b0 = pool.Bytes();
    EXPECT(b0 > 0 && !pool.ShouldDrain());
    while (!pool.ShouldDrain()) { SeedHeader h; h.n = 8; pool.Begin(h); }
    EXPECT(pool.Bytes() >= pool.ByteLimit());
    pool.Clear();
    EXPECT(pool.Empty() && pool.Bytes() == 0);
}

static void TestSparse()
{
    std::printf("SparseAccum\n");
    SparseAccum a(4); // force several rehashes
    std::map<std::pair<int, int>, std::array<double, 4>> ref;
    std::mt19937 rng(1234);
    for (int i = 0; i < 200000; ++i)
    {
        const int x = rng() % 300, y = rng() % 200;
        const float X = (rng() % 1000) * 0.001f, Y = (rng() % 1000) * 0.001f, Z = (rng() % 1000) * 0.001f;
        const float p = float(rng() % 3);
        a.Add(x, y, X, Y, Z, p);
        auto &r = ref[{x, y}]; r[0] += X; r[1] += Y; r[2] += Z; r[3] += p;
    }
    EXPECT(a.Size() == ref.size());
    size_t seen = 0; bool match = true;
    a.ForEach([&](int32_t x, int32_t y, const float v[4])
    {
        ++seen; auto it = ref.find({x, y});
        if (it == ref.end()) { match = false; return; }
        for (int c = 0; c < 4; ++c)
            if (std::fabs(v[c] - it->second[c]) > 1e-2 * (1.0 + std::fabs(it->second[c]))) match = false;
    });
    EXPECT(seen == ref.size() && match);
    a.Clear();
    EXPECT(a.Empty());
    a.Add(0, 0, 1, 2, 3, 4); // (0,0) must be a valid key (key encoding is +1)
    EXPECT(a.Size() == 1);
}

static void TestSmooth()
{
    std::printf("Smoothing\n");
    FilterGrid g; g.Init(40, 30);
    for (auto &v : g.v) v = 7.f;
    FilterGrid out; BoxBlur(g, out, 3);
    bool flat = true; for (float v : out.v) flat &= (std::fabs(v - 7.f) < 1e-4f);
    EXPECT(flat); // edge renormalisation: a constant grid stays constant to the border

    // A single hot pixel spreads to (2r+1)^2 and conserves total mass.
    g.v.assign(g.v.size(), 0.f); g.Add(20, 15, 49.f);
    BoxBlur(g, out, 3);
    double tot = 0; for (float v : out.v) tot += v;
    EXPECT(std::fabs(tot - 49.0) < 1e-3);
    EXPECT(std::fabs(out.v[15 * 40 + 20] - 1.f) < 1e-5f);

    // expected/actual: under-visited pixel is boosted, no-proposal pixel untouched.
    EXPECT(SmoothScale(0.f, 5.f, 16.f) == 1.f);
    EXPECT(std::fabs(SmoothScale(2.f, 4.f, 16.f) - 2.f) < 1e-6f);
    EXPECT(SmoothScale(0.01f, 100.f, 16.f) == 16.f);

    // Apply: uniformly-visited image is a fixed point of the filter.
    FilterGrid act; act.Init(8, 8); for (auto &v : act.v) v = 10.f;
    std::vector<float> pix(8 * 8 * 4, 3.f);
    ApplyProposalFilter(pix.data(), 4, 3, act, 2);
    bool fixed = true; for (size_t i = 0; i < 64; ++i) for (int c = 0; c < 3; ++c) fixed &= (std::fabs(pix[i * 4 + c] - 3.f) < 1e-5f);
    EXPECT(fixed);
}

static void TestMath()
{
    std::printf("Math\n");
    // Same key -> same stream; different chain index -> different stream.
    Pcg32 a = KeyedPcg(1, 2, 3), b = KeyedPcg(1, 2, 3), c = KeyedPcg(1, 2, 4);
    bool same = true, diff = false;
    for (int i = 0; i < 64; ++i) { const uint32_t x = a.NextU32(); same &= (x == b.NextU32()); diff |= (x != c.NextU32()); }
    EXPECT(same && diff);
    // Uniform floats stay in [0,1).
    Pcg32 r = KeyedPcg(9, 9, 9); bool inr = true;
    for (int i = 0; i < 100000; ++i) { const float f = r.NextFloat(); inr &= (f >= 0.f && f < 1.f); }
    EXPECT(inr);
    EXPECT(AcceptProb(1.f, 2.f, 1.f) == 1.f);
    EXPECT(std::fabs(AcceptProb(2.f, 1.f, 1.f) - 0.5f) < 1e-7f);
    EXPECT(std::fabs(AcceptProb(2.f, 1.f, 3.f) - 1.f) < 1e-7f); // T ratio feeds in
    EXPECT(AcceptProb(1.f, 0.f, 1.f) == 0.f);
    EXPECT(AcceptProb(1.f, NAN, 1.f) == 0.f);
    EXPECT(AcceptProb(1.f, 1.f, NAN) == 0.f);
    EXPECT(AcceptProb(0.f, 1.f, 1.f) == 1.f); // corrupt state escapes
    bool cl;
    EXPECT(NumChains(0.f, 10, 1.f, 0.5f, 100, &cl) == 0 && !cl);
    EXPECT(NumChains(-1.f, 10, 1.f, 0.5f, 100, &cl) == 0);
    EXPECT(NumChains(NAN, 10, 1.f, 0.5f, 100, &cl) == 0);
    EXPECT(NumChains(1e9f, 10, 1.f, 0.5f, 100, &cl) == 100 && cl);
}

int main()
{
    TestPool(); TestSparse(); TestSmooth(); TestMath();
    std::printf("%s (%d failures)\n", g_fail ? "FAILED" : "PASSED", g_fail);
    return g_fail ? 1 : 0;
}
