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

    // Field order is load-bearing: a transposed record kernel addresses rows by
    // raw word index, so reordering LUX2_GEOM_FIELDS/LUX2_MIS_FIELDS would
    // corrupt renders with no compile error. Pin every offset.
    static_assert(kWord_p == 0u, "geom field order: p");
    static_assert(kWord_n == 3u, "geom field order: n");
    static_assert(kWord_ng == 6u, "geom field order: ng");
    static_assert(kWord_wb == 9u, "geom field order: wb");
    static_assert(kWord_dp_du == 12u, "geom field order: dp_du");
    static_assert(kWord_dp_dv == 15u, "geom field order: dp_dv");
    static_assert(kWord_dp_ds == 18u, "geom field order: dp_ds");
    static_assert(kWord_dp_dt == 21u, "geom field order: dp_dt");
    static_assert(kWord_uv == 24u, "geom field order: uv");
    static_assert(kWord_flux == 26u, "geom field order: flux");
    static_assert(kWord_k == 27u, "geom field order: k");
    static_assert(kWord_matID == 28u, "geom field order: matID");
    static_assert(kWord_medID == 29u, "geom field order: medID");
    static_assert(kWord_medFront == 30u, "geom field order: medFront");
    static_assert(kWord_medBack == 31u, "geom field order: medBack");
    static_assert(GEOM_WORDS == 32u, "geom record is 32 words");
    static_assert(kWord_dAW == 0u, "mis field order: dAW");
    static_assert(kWord_dARW == 1u, "mis field order: dARW");
    static_assert(kWord_rr == 2u, "mis field order: rr");
    static_assert(kWord_rrR == 3u, "mis field order: rrR");
    static_assert(kWord_cosi == 4u, "mis field order: cosi");
    static_assert(kWord_coso == 5u, "mis field order: coso");
    static_assert(kWord_d2 == 6u, "mis field order: d2");
    static_assert(kWord_bits == 7u, "mis field order: bits");
    static_assert(MIS_WORDS_TOTAL == 8u, "mis record is 8 words");
    std::cout << "  ok:   geom/mis field order pinned (compile-time)" << std::endl;

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

    // Masked round-trip. A partial-mask store must leave inactive lanes'
    // records untouched, and a partial-mask load must return zero for inactive
    // lanes (enoki merges every gather from setzero). A transposed record
    // kernel replaces both paths, so pin the contract before that lands.
    {
        const uint32_t sentinelG = 0xDEADBEEFu, sentinelM = 0x5A5A5A5Au;
        for (auto &w : arena.geomWords)
            w = sentinelG;
        for (auto &w : arena.misWords)
            w = sentinelM;

        // Distinct records with stride 4 so rec +/- 1 is never written.
        std::vector<uint32_t> pathArr(PACKET_WIDTH), vArr(PACKET_WIDTH);
        std::vector<float> mA(PACKET_WIDTH);
        for (size_t i = 0; i < PACKET_WIDTH; ++i)
        {
            pathArr[i] = uint32_t(i % nPaths);
            vArr[i] = uint32_t((i / nPaths) * 4u + 2u);
            mA[i] = (i % 3u) != 0u ? 1.f : 0.f; // mixed fill, not alternating
        }
        const UInt32P path = enoki::load_unaligned<UInt32P>(pathArr.data());
        const Int32P v = enoki::load_unaligned<Int32P>(
            reinterpret_cast<const int32_t *>(vArr.data()));
        const MaskP part =
            enoki::load_unaligned<FloatP>(mA.data()) > FloatP(0.f);
        const UInt32P rec = RecIdx(ps, path, v, MaskP(true));
        std::vector<uint32_t> recArr(PACKET_WIDTH), act(PACKET_WIDTH);
        enoki::store_unaligned(recArr.data(), rec);
        enoki::store_unaligned(act.data(),
                               select(part, UInt32P(1u), UInt32P(0u)));

        // Expected raw word per (lane, word): float fields keep the finite
        // masking BitsToFloat applies, U fields are stored verbatim.
        auto fword = [](uint32_t u)
        {
            const float f = BitsToFloat(u);
            uint32_t r;
            std::memcpy(&r, &f, sizeof(r));
            return r;
        };
        auto expG = [&](size_t i, uint32_t w)
        {
            const uint32_t m = Mix(uint32_t(i), w);
            return w >= 28u ? m : fword(m);
        };
        auto expM = [&](size_t i, uint32_t w)
        {
            const uint32_t m = Mix(uint32_t(i), 100u + w);
            return w == 7u ? m : fword(m);
        };

        VertexGeomP g;
        auto fillF = [&](uint32_t word)
        {
            std::vector<float> f(PACKET_WIDTH);
            for (size_t i = 0; i < PACKET_WIDTH; ++i)
                f[i] = BitsToFloat(Mix(uint32_t(i), word));
            return enoki::load_unaligned<FloatP>(f.data());
        };
        auto fillU = [&](uint32_t word)
        {
            std::vector<uint32_t> u(PACKET_WIDTH);
            for (size_t i = 0; i < PACKET_WIDTH; ++i)
                u[i] = Mix(uint32_t(i), word);
            return enoki::load_unaligned<UInt32P>(u.data());
        };
        g.p = Point3fP(fillF(0), fillF(1), fillF(2));
        g.n = Point3fP(fillF(3), fillF(4), fillF(5));
        g.ng = Point3fP(fillF(6), fillF(7), fillF(8));
        g.wb = Point3fP(fillF(9), fillF(10), fillF(11));
        g.dp_du = Point3fP(fillF(12), fillF(13), fillF(14));
        g.dp_dv = Point3fP(fillF(15), fillF(16), fillF(17));
        g.dp_ds = Point3fP(fillF(18), fillF(19), fillF(20));
        g.dp_dt = Point3fP(fillF(21), fillF(22), fillF(23));
        g.uv = Point2fP(fillF(24), fillF(25));
        g.flux = fillF(26);
        g.k = fillF(27);
        g.matID = fillU(28);
        g.medID = fillU(29);
        g.medFront = fillU(30);
        g.medBack = fillU(31);

        VertexMisP mi;
        mi.dAW = fillF(100);
        mi.dARW = fillF(101);
        mi.rr = fillF(102);
        mi.rrR = fillF(103);
        mi.cosi = fillF(104);
        mi.coso = fillF(105);
        mi.d2 = fillF(106);
        mi.bits = fillU(107);

        StoreGeom(arena.geomWords.data(), g, rec, part);
        StoreMis(arena.misWords.data(), mi, rec, part);

        bool okActive = true, okUntouched = true;
        for (size_t i = 0; i < PACKET_WIDTH; ++i)
        {
            const size_t gb = size_t(recArr[i]) * GEOM_WORDS;
            const size_t mb = size_t(recArr[i]) * MIS_WORDS;
            for (uint32_t w = 0; w < GEOM_WORDS; ++w)
            {
                const uint32_t got = arena.geomWords[gb + w];
                if (act[i])
                    okActive &= got == expG(i, w);
                else
                    okUntouched &= got == sentinelG;
            }
            for (uint32_t w = 0; w < MIS_WORDS; ++w)
            {
                const uint32_t got = arena.misWords[mb + w];
                if (act[i])
                    okActive &= got == expM(i, w);
                else
                    okUntouched &= got == sentinelM;
            }
        }
        Check(okActive, "partial-mask store writes active lanes exactly");
        Check(okUntouched, "partial-mask store leaves inactive records intact");

        // Neighbouring records must survive a partial store.
        bool okNeigh = true;
        for (size_t i = 0; i < PACKET_WIDTH; ++i)
        {
            for (int32_t d = -1; d <= 1; d += 2)
            {
                const size_t nb = size_t(int64_t(recArr[i]) + d);
                for (uint32_t w = 0; w < GEOM_WORDS; ++w)
                    okNeigh &= arena.geomWords[nb * GEOM_WORDS + w] == sentinelG;
                for (uint32_t w = 0; w < MIS_WORDS; ++w)
                    okNeigh &= arena.misWords[nb * MIS_WORDS + w] == sentinelM;
            }
        }
        Check(okNeigh, "partial-mask store leaves rec +/- 1 intact");

        // Partial-mask load: active lanes read back, inactive lanes are zero.
        const VertexGeomP g3 = LoadGeom(arena.geomWords.data(), rec, part);
        const VertexMisP mi3 = LoadMis(arena.misWords.data(), rec, part);
        bool okLoad = true;
        auto chk = [&](const UInt32P &raw, uint32_t w, bool geomWord)
        {
            std::vector<uint32_t> ua(PACKET_WIDTH);
            enoki::store_unaligned(ua.data(), raw);
            for (size_t i = 0; i < PACKET_WIDTH; ++i)
            {
                const uint32_t exp =
                    act[i] ? (geomWord ? expG(i, w) : expM(i, w)) : 0u;
                if (ua[i] != exp)
                {
                    std::cerr << "    load mismatch word " << w << " lane " << i
                              << " got 0x" << std::hex << ua[i] << " want 0x"
                              << exp << std::dec << "\n";
                    okLoad = false;
                }
            }
        };
        auto chkG = [&](const FloatP &a, uint32_t w)
        { chk(reinterpret_array<UInt32P>(a), w, true); };
        auto chkM = [&](const FloatP &a, uint32_t w)
        { chk(reinterpret_array<UInt32P>(a), w, false); };
        chkG(g3.p.x(), 0);
        chkG(g3.p.y(), 1);
        chkG(g3.p.z(), 2);
        chkG(g3.n.x(), 3);
        chkG(g3.n.z(), 5);
        chkG(g3.ng.z(), 8);
        chkG(g3.wb.x(), 9);
        chkG(g3.dp_du.y(), 13);
        chkG(g3.dp_dt.z(), 23);
        chkG(g3.uv.x(), 24);
        chkG(g3.uv.y(), 25);
        chkG(g3.flux, 26);
        chkG(g3.k, 27);
        chk(g3.matID, 28, true);
        chk(g3.medID, 29, true);
        chk(g3.medFront, 30, true);
        chk(g3.medBack, 31, true);
        chkM(mi3.dAW, 0);
        chkM(mi3.dARW, 1);
        chkM(mi3.rr, 2);
        chkM(mi3.rrR, 3);
        chkM(mi3.cosi, 4);
        chkM(mi3.coso, 5);
        chkM(mi3.d2, 6);
        chk(mi3.bits, 7, false);
        Check(okLoad, "partial-mask load zeroes inactive lanes");

        // Empty mask must be a no-op store.
        std::vector<uint32_t> snapG(arena.geomWords.begin(), arena.geomWords.end());
        StoreGeom(arena.geomWords.data(), g, rec, MaskP(false));
        Check(snapG == std::vector<uint32_t>(arena.geomWords.begin(),
                                             arena.geomWords.end()),
              "store with empty mask writes nothing");

        // Single-word patch into a full record and into rec-1: the production
        // MIS pattern (dAW/d2 written on a later step). Only the target word
        // may change. This is why MIS cannot use a whole-line store.
        StoreGeom(arena.geomWords.data(), g, rec, MaskP(true));
        StoreMis(arena.misWords.data(), mi, rec, MaskP(true));
        const uint32_t patch = 0x12345678u;
        StoreWord(arena.misWords.data(), UInt32P(patch),
                  RecWord<MIS_WORDS>(rec, kWord_dAW), part);
        StoreWord(arena.misWords.data(), UInt32P(patch),
                  RecWord<MIS_WORDS>(rec - Int32P(1), kWord_d2), part);
        bool okPatch = true;
        for (size_t i = 0; i < PACKET_WIDTH; ++i)
        {
            const size_t mb = size_t(recArr[i]) * MIS_WORDS;
            const size_t pb = size_t(recArr[i] - 1u) * MIS_WORDS;
            for (uint32_t w = 0; w < MIS_WORDS; ++w)
            {
                const uint32_t got = arena.misWords[mb + w];
                const uint32_t exp =
                    (act[i] && w == kWord_dAW) ? patch : expM(i, w);
                okPatch &= got == exp;
                const uint32_t gotP = arena.misWords[pb + w];
                const uint32_t expP =
                    (act[i] && w == kWord_d2) ? patch : sentinelM;
                okPatch &= gotP == expP;
            }
        }
        Check(okPatch, "word patches hit only their target word");
    }

    if (g_failures)
    {
        std::cerr << g_failures << " storage check(s) failed" << std::endl;
        return 1;
    }
    std::cout << "storage checks passed" << std::endl;
    return 0;
}
