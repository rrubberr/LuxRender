/***************************************************************************
 *   Gate test for the wavefront-unified refactor, Phase 1 step A.         *
 *                                                                         *
 *   Purpose: decide (before touching the tree) whether ENOKI_STRUCT_SUPPORT *
 *   generates working select/gather/scatter/any on templated core types    *
 *   that NEST another struct and hold a mask_t member -- exactly the shape *
 *   LaneCoreT will have. If nested select does not compile, the plan falls *
 *   back to an X-macro Blend generator (recorded in wavefront-unified.md). *
 *                                                                         *
 *   Self-contained: compiles against enoki headers only, no lux2 tree.    *
 *   Field lists mirror core/ray.h RayP and core/color.h SpectrumWavelengthsP
 *   so a pass here is meaningful for the real conversion (E1/E2).          *
 *                                                                         *
 *   IMPORTANT: ENOKI_STRUCT_SUPPORT must be at GLOBAL scope with a fully-   *
 *   qualified name, exactly as src/lux2/lights/area.h does. Nesting it in   *
 *   the namespace breaks the macro's nested enoki:: references.             *
 ***************************************************************************/

#include <enoki/array.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace lux2 {

#if defined(__AVX512F__)
constexpr size_t W = 16;
#elif defined(__AVX2__)
constexpr size_t W = 8;
#elif defined(__SSE4_2__)
constexpr size_t W = 4;
#else
constexpr size_t W = 1;
#endif

using Float = float;
using PacketF = enoki::Packet<Float, W>;
using Mask = enoki::mask_t<PacketF>;
using UInt32P = enoki::Packet<std::uint32_t, W>;
using Int32P = enoki::Packet<int, W>;
using Vec3fP = enoki::Array<PacketF, 3>;

// Mirror of SpectrumWavelengthsP (core/color.h): plain packet fields.
template <typename Value> struct SpectrumWavelengthsT {
    using FloatV = Value;
    using Int32V = enoki::int32_array_t<Value>;
    FloatV w;
    Int32V binRGB;
    FloatV offsetRGB;
    Int32V binXYZ;
    FloatV offsetXYZ;
    ENOKI_STRUCT(SpectrumWavelengthsT, w, binRGB, offsetRGB, binXYZ, offsetXYZ)
};

// Mirror of RayP (core/ray.h): the LIVE field set after E1's dead-field drop.
template <typename Value> struct RayT {
    using Vec3 = enoki::Array<Value, 3>;
    using UInt32 = enoki::uint32_array_t<Value>;
    using Int32 = enoki::int32_array_t<Value>;
    Vec3 o, d, d_rcp;
    Value mint, maxt, time, wavelengths;
    UInt32 medium, mask;
    Value throughput, pdf;
    Int32 depth;
    enoki::mask_t<Value> specularBounce, alive;
    ENOKI_STRUCT(RayT, o, d, d_rcp, mint, maxt, time, wavelengths, medium, mask,
                              throughput, pdf, depth, specularBounce, alive)
};

// The nested lane struct F2 will build: a RayT + a spectrum + seed streams +
// its OWN semantic alive mask (the mirror lives on ray.alive at intersect).
template <typename Value> struct LaneCoreT {
    using UInt32 = enoki::uint32_array_t<Value>;
    RayT<Value> ray;
    SpectrumWavelengthsT<Value> sw;
    UInt32 seed, sampleIdx;
    enoki::mask_t<Value> alive;
    ENOKI_STRUCT(LaneCoreT, ray, sw, seed, sampleIdx, alive)
};

} // namespace lux2

// --- struct_support specializations: GLOBAL scope, fully qualified (area.h pattern) ---
ENOKI_STRUCT_SUPPORT(lux2::SpectrumWavelengthsT, w, binRGB, offsetRGB, binXYZ, offsetXYZ)
ENOKI_STRUCT_SUPPORT(lux2::RayT, o, d, d_rcp, mint, maxt, time, wavelengths, medium,
                                  mask, throughput, pdf, depth, specularBounce, alive)
ENOKI_STRUCT_SUPPORT(lux2::LaneCoreT, ray, sw, seed, sampleIdx, alive)

namespace lux2 {

using RayP = RayT<PacketF>;
using SpectrumWavelengthsP = SpectrumWavelengthsT<PacketF>;
using LaneCore = LaneCoreT<PacketF>;

static int g_fail = 0;
#define CHECK(cond, name)                                                      \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("  FAIL: %s\n", name);                                 \
            ++g_fail;                                                          \
        } else {                                                               \
            std::printf("  ok:   %s\n", name);                                 \
        }                                                                      \
    } while (0)

} // namespace lux2

int main() {
    using namespace lux2;

    const Mask m = enoki::arange<PacketF>() < PacketF(4.f); // first 4 lanes true

    // ---- 1. select on RayP (the one-blend core operation) ----------------
    RayP a, b;
    a.o = Vec3fP{PacketF(1.f), PacketF(2.f), PacketF(3.f)};
    a.d = Vec3fP{PacketF(4.f), PacketF(5.f), PacketF(6.f)};
    a.mint = PacketF(0.f);
    a.alive = Mask(true);
    b.o = Vec3fP{PacketF(10.f), PacketF(20.f), PacketF(30.f)};
    b.d = Vec3fP{PacketF(40.f), PacketF(50.f), PacketF(60.f)};
    b.mint = PacketF(9.f);
    b.alive = Mask(false);
    RayP c = enoki::select(m, a, b);
    CHECK(enoki::all(c.o.x() == select(m, a.o.x(), b.o.x())),
          "RayP select: o.x blends per lane");
    CHECK(enoki::all(c.mint == select(m, a.mint, b.mint)),
          "RayP select: scalar field blends");
    CHECK(enoki::all(c.alive == select(m, a.alive, b.alive)),
          "RayP select: mask_t field blends");

    // ---- 2. any()/packets() on RayP (used by PlanPixels dead-lane count) --
    CHECK(enoki::packets(a) == W, "RayP packets() == W");
    CHECK(static_cast<int>(enoki::count(enoki::any(a.o))) == static_cast<int>(W),
          "RayP any()/count over a Vec3 field");

    // ---- 3. gather / scatter on RayP from a raw buffer -------------------
    alignas(64) RayP pool[8];
    for (unsigned i = 0; i < 8; ++i) {
        pool[i].o = Vec3fP{PacketF(float(i)), PacketF(float(i)), PacketF(float(i))};
        pool[i].mint = PacketF(float(i));
        pool[i].alive = Mask(true);
    }
    const UInt32P idx = enoki::arange<UInt32P>() + UInt32P(2u); // 2..W+1, wrap
    RayP g = enoki::gather<RayP>(&pool[0], idx, m);
    CHECK(enoki::all(g.o.x() == select(m, pool[2].o.x(), g.o.x())),
          "RayP gather: o.x from indexed records under mask");

    RayP sink[8];
    for (unsigned i = 0; i < 8; ++i) {
        sink[i].o = Vec3fP{PacketF(0.f), PacketF(0.f), PacketF(0.f)};
        sink[i].mint = PacketF(-1.f);
        sink[i].alive = Mask(false);
    }
    enoki::scatter(&sink[0], c, idx, m);
    CHECK(enoki::all(sink[2].o.x() == select(m, c.o.x(), sink[2].o.x())),
          "RayP scatter: writes indexed records under mask");

    // ---- 4. nested LaneCore: select (the F2 one-blend) -------------------
    LaneCore la, lb;
    la.ray = a;
    la.sw.w = PacketF(1.f);
    la.seed = UInt32P(111u);
    la.alive = Mask(true);
    lb.ray = b;
    lb.sw.w = PacketF(2.f);
    lb.seed = UInt32P(222u);
    lb.alive = Mask(false);
    LaneCore lc = enoki::select(m, la, lb);
    CHECK(enoki::all(lc.ray.o.x() == select(m, la.ray.o.x(), lb.ray.o.x())),
          "LaneCore select: nested RayT field blends");
    CHECK(enoki::all(lc.sw.w == select(m, la.sw.w, lb.sw.w)),
          "LaneCore select: nested spectrum field blends");
    CHECK(enoki::all(lc.seed == select(m, la.seed, lb.seed)),
          "LaneCore select: scalar stream blends");
    CHECK(enoki::all(lc.alive == select(m, la.alive, lb.alive)),
          "LaneCore select: lane's own mask_t blends");

    // ---- 5. nested LaneCore: gather / scatter ----------------------------
    alignas(64) LaneCore lanes[8];
    for (unsigned i = 0; i < 8; ++i) {
        lanes[i].ray.o = Vec3fP{PacketF(float(i)), PacketF(float(i)), PacketF(float(i))};
        lanes[i].seed = UInt32P(i * 7u);
        lanes[i].alive = Mask(true);
    }
    LaneCore lg = enoki::gather<LaneCore>(&lanes[0], idx, m);
    CHECK(enoki::all(lg.ray.o.x() == select(m, lanes[2].ray.o.x(), lg.ray.o.x())),
          "LaneCore gather: nested RayT.o from indexed records");

    LaneCore lsink[8];
    for (unsigned i = 0; i < 8; ++i) {
        lsink[i].ray.o = Vec3fP{PacketF(0.f), PacketF(0.f), PacketF(0.f)};
        lsink[i].alive = Mask(false);
    }
    enoki::scatter(&lsink[0], lc, idx, m);
    CHECK(enoki::all(lsink[2].ray.o.x() == select(m, lc.ray.o.x(), lsink[2].ray.o.x())),
          "LaneCore scatter: nested RayT.o to indexed records");

    // ---- 6. sanity: a real blend produces the expected lane mix ----------
    LaneCore fresh = enoki::select(m, la, lb);
    alignas(64) float seedV[W];
    enoki::store_unaligned(seedV, fresh.seed);
    bool mix_ok = true;
    for (unsigned i = 0; i < W; ++i) {
        const float want = (m[i] ? 111.f : 222.f);
        if (seedV[i] != want) mix_ok = false;
    }
    CHECK(mix_ok, "LaneCore blend: lane mix is per-lane correct");

    std::printf("\n%s (%d failure(s))\n", g_fail ? "GATE FAILED" : "GATE PASSED",
                g_fail);
    return g_fail ? 1 : 0;
}
