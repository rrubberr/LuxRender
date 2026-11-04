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

// Phase 4a-4c: validate the generated record layout, RecIdx addressing and the
// v1 degenerate arena. Store a full packet of vertices across several paths and
// blocks, then load them back and require bit-exact equality per lane.

#include "integrators/wf/storage.h"

#include <cmath>
#include <cstdint>
#include <cstring>
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
    else
    {
        std::cout << "  ok:   " << what << std::endl;
    }
}

// Deterministic pseudo-random uint32 per (lane, word).
uint32_t Mix(uint32_t a, uint32_t b)
{
    uint64_t x = uint64_t(a) * 0x9E3779B97F4A7C15ull + uint64_t(b) * 0xBF58476D1CE4E5B9ull;
    x ^= x >> 30;
    x *= 0x94D049BB133111EBull;
    x ^= x >> 31;
    return uint32_t(x);
}

float BitsToFloat(uint32_t u)
{
    // Keep it finite and non-NaN for the float fields.
    u &= 0x7F7FFFFFu; // clear sign+exp-max -> finite positive normal-ish
    float f;
    std::memcpy(&f, &u, sizeof(f));
    return f;
}
} // namespace

int main()
{
    std::cout << "lux2 wf storage_check (Phase 4a-4c)" << std::endl;

    Check(GEOM_WORDS == 32u, "GEOM_WORDS == 32");
    Check(MIS_WORDS == 8u, "MIS_WORDS == 8");
    Check(kWord_matID == 28u, "matID word offset == 28 (after 8*V3+V2+2F)");

    const uint32_t nPaths = 4;
    BlockArena arena;
    PathStore ps;
    ps.Init(&arena, nPaths);

    // RecIdx: path p, vertex v -> p*PATH_STRIDE + v.
    {
        UInt32P path(0u), v(0u);
        std::vector<uint32_t> pathArr(PACKET_WIDTH), vArr(PACKET_WIDTH);
        for (size_t i = 0; i < PACKET_WIDTH; ++i)
        {
            pathArr[i] = uint32_t(i % nPaths);
            vArr[i] = uint32_t((i * 5 + 1) % MAX_DEPTH);
        }
        path = enoki::load_unaligned<UInt32P>(pathArr.data());
        v = enoki::load_unaligned<Int32P>(reinterpret_cast<const int32_t *>(vArr.data()));
        const UInt32P rec = RecIdx(ps, path, v, MaskP(true));
        std::vector<uint32_t> recArr(PACKET_WIDTH);
        enoki::store_unaligned(recArr.data(), rec);
        bool ok = true;
        for (size_t i = 0; i < PACKET_WIDTH; ++i)
        {
            const uint32_t expect =
                pathArr[i] * uint32_t(PATH_STRIDE) + vArr[i];
            if (recArr[i] != expect)
                ok = false;
        }
        Check(ok, "RecIdx maps (path,v) to path*PATH_STRIDE+v");
    }

    // Round-trip: store a distinct geom+mis record per lane at scattered
    // (path,vertex), then load back and compare every field bit-exactly.
    {
        std::vector<uint32_t> pathArr(PACKET_WIDTH), vArr(PACKET_WIDTH);
        for (size_t i = 0; i < PACKET_WIDTH; ++i)
        {
            pathArr[i] = uint32_t(i % nPaths);
            // Spread across two blocks (v in [0,16)) to exercise blockTbl.
            vArr[i] = uint32_t((i * 3) % 16);
        }
        const UInt32P path = enoki::load_unaligned<UInt32P>(pathArr.data());
        const Int32P v = enoki::load_unaligned<Int32P>(
            reinterpret_cast<const int32_t *>(vArr.data()));
        const MaskP all(true);

        VertexGeomP g;
        VertexMisP mi;
        // Fill each field with a distinct deterministic pattern per lane.
        auto fillF = [&](int word)
        {
            std::vector<float> f(PACKET_WIDTH);
            for (size_t i = 0; i < PACKET_WIDTH; ++i)
                f[i] = BitsToFloat(Mix(uint32_t(i), uint32_t(word)));
            return enoki::load_unaligned<FloatP>(f.data());
        };
        auto fillU = [&](int word)
        {
            std::vector<uint32_t> u(PACKET_WIDTH);
            for (size_t i = 0; i < PACKET_WIDTH; ++i)
                u[i] = Mix(uint32_t(i), uint32_t(word));
            return enoki::load_unaligned<UInt32P>(u.data());
        };
        g.p = Point3fP(fillF(1), fillF(2), fillF(3));
        g.n = Point3fP(fillF(4), fillF(5), fillF(6));
        g.ng = Point3fP(fillF(7), fillF(8), fillF(9));
        g.wb = Point3fP(fillF(10), fillF(11), fillF(12));
        g.dp_du = Point3fP(fillF(13), fillF(14), fillF(15));
        g.dp_dv = Point3fP(fillF(16), fillF(17), fillF(18));
        g.dp_ds = Point3fP(fillF(19), fillF(20), fillF(21));
        g.dp_dt = Point3fP(fillF(22), fillF(23), fillF(24));
        g.uv = Point2fP(fillF(25), fillF(26));
        g.flux = fillF(27);
        g.k = fillF(28);
        g.matID = fillU(29);
        g.medID = fillU(30);
        g.medFront = fillU(31);
        g.medBack = fillU(32);

        mi.dAW = fillF(40);
        mi.dARW = fillF(41);
        mi.rr = fillF(42);
        mi.rrR = fillF(43);
        mi.cosi = fillF(44);
        mi.coso = fillF(45);
        mi.d2 = fillF(46);
        mi.bits = fillU(47);

        const UInt32P rec = RecIdx(ps, path, v, all);
        StoreGeom(arena.geomWords.data(), g, rec, all);
        StoreMis(arena.misWords.data(), mi, rec, all);

        // Load back into a fresh packet (start from garbage to catch misses).
        VertexGeomP g2;
        VertexMisP mi2;
        const UInt32P rec2 = RecIdx(ps, path, v, all);
        g2 = LoadGeom(arena.geomWords.data(), rec2, all);
        mi2 = LoadMis(arena.misWords.data(), rec2, all);

        // Compare bit-exactly via raw uint32 of each field.
        auto cmpF = [&](const FloatP &a, const FloatP &b, const char *nm)
        {
            std::vector<uint32_t> ua(PACKET_WIDTH), ub(PACKET_WIDTH);
            enoki::store_unaligned(ua.data(), reinterpret_array<UInt32P>(a));
            enoki::store_unaligned(ub.data(), reinterpret_array<UInt32P>(b));
            for (size_t i = 0; i < PACKET_WIDTH; ++i)
                if (ua[i] != ub[i])
                {
                    std::cerr << "    mismatch " << nm << " lane " << i << "\n";
                    g_failures++;
                    return;
                }
        };
        auto cmp3 = [&](const Point3fP &a, const Point3fP &b, const char *nm)
        {
            cmpF(a.x(), b.x(), nm);
            cmpF(a.y(), b.y(), nm);
            cmpF(a.z(), b.z(), nm);
        };
        auto cmpU = [&](const UInt32P &a, const UInt32P &b, const char *nm)
        {
            std::vector<uint32_t> ua(PACKET_WIDTH), ub(PACKET_WIDTH);
            enoki::store_unaligned(ua.data(), a);
            enoki::store_unaligned(ub.data(), b);
            for (size_t i = 0; i < PACKET_WIDTH; ++i)
                if (ua[i] != ub[i])
                {
                    std::cerr << "    mismatch " << nm << " lane " << i << "\n";
                    g_failures++;
                    return;
                }
        };
        const int before = g_failures;
        cmp3(g.p, g2.p, "p");
        cmp3(g.n, g2.n, "n");
        cmp3(g.ng, g2.ng, "ng");
        cmp3(g.wb, g2.wb, "wb");
        cmp3(g.dp_du, g2.dp_du, "dp_du");
        cmp3(g.dp_dv, g2.dp_dv, "dp_dv");
        cmp3(g.dp_ds, g2.dp_ds, "dp_ds");
        cmp3(g.dp_dt, g2.dp_dt, "dp_dt");
        cmpF(g.uv.x(), g2.uv.x(), "uv.x");
        cmpF(g.uv.y(), g2.uv.y(), "uv.y");
        cmpF(g.flux, g2.flux, "flux");
        cmpF(g.k, g2.k, "k");
        cmpU(g.matID, g2.matID, "matID");
        cmpU(g.medID, g2.medID, "medID");
        cmpU(g.medFront, g2.medFront, "medFront");
        cmpU(g.medBack, g2.medBack, "medBack");
        Check(before == g_failures, "geom round-trip bit-exact (all 32 words)");

        const int before2 = g_failures;
        cmpF(mi.dAW, mi2.dAW, "dAW");
        cmpF(mi.dARW, mi2.dARW, "dARW");
        cmpF(mi.rr, mi2.rr, "rr");
        cmpF(mi.rrR, mi2.rrR, "rrR");
        cmpF(mi.cosi, mi2.cosi, "cosi");
        cmpF(mi.coso, mi2.coso, "coso");
        cmpF(mi.d2, mi2.d2, "d2");
        cmpU(mi.bits, mi2.bits, "bits");
        Check(before2 == g_failures, "mis round-trip bit-exact (all 8 words)");
    }

    if (g_failures)
    {
        std::cerr << g_failures << " storage check(s) failed" << std::endl;
        return 1;
    }
    std::cout << "storage checks passed" << std::endl;
    return 0;
}
