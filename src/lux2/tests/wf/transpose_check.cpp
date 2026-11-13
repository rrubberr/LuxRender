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

// Validate the record transposes against TransposeScalar with randomized
// inputs. These kernels replace per-word gathers in the vertex load/store
// paths, so a wrong shuffle silently corrupts renders instead of failing.

#include "core/math/simd_transpose.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace lux2;

namespace
{
int g_failures = 0;

void Report(const char *name, bool ok)
{
    std::printf("  %s %s\n", ok ? "ok:  " : "FAIL:", name);
    if (!ok)
        ++g_failures;
}

// Splitmix64: deterministic per seed, no library dependency.
uint64_t Next(uint64_t &s)
{
    s += 0x9E3779B97F4A7C15ull;
    uint64_t z = s;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// Fill with random words, including bit patterns that stress sign/exponent
// and lane identity (the fixed r*100+c pattern hides swapped lanes of equal
// magnitude).
void FillRandom(std::uint32_t *p, size_t n, uint64_t seed)
{
    for (size_t i = 0; i < n; ++i)
    {
        const uint64_t r = Next(seed);
        switch (i % 4)
        {
        case 0: p[i] = uint32_t(r); break;                       // arbitrary
        case 1: p[i] = uint32_t(r) & 0x80000000u; break;         // sign only
        case 2: p[i] = uint32_t(r) | 0xFF800000u; break;         // exp max
        default: p[i] = uint32_t(i) | (uint32_t(r & 0xF) << 16); break;
        }
    }
}

// Square memory transpose through `kernel`, compared to scalar.
template <class Kernel>
bool CheckSquare(Kernel &&kernel)
{
    constexpr int N = 8;
    alignas(64) std::uint32_t in[N][N], out[N][N], ref[N][N];
    for (int seed = 0; seed < 64; ++seed)
    {
        uint64_t s = 0x1234567ull + uint64_t(seed) * 7919ull;
        FillRandom(&in[0][0], N * N, s);
        TransposeScalar(&in[0][0], &ref[0][0], N, N);
        std::memset(out, 0xCC, sizeof out);
        kernel(in, out);
        if (std::memcmp(out, ref, sizeof out) != 0)
        {
            for (int r = 0; r < N; ++r)
                for (int c = 0; c < N; ++c)
                    if (out[r][c] != ref[r][c])
                    {
                        std::printf("    seed %d: out[%d][%d]=0x%08X want 0x%08X\n",
                                    seed, r, c, out[r][c], ref[r][c]);
                        return false;
                    }
        }
    }
    return true;
}
} // namespace

int main()
{
    std::printf("lux2 wf transpose_check\n");

#if defined(__AVX512F__)
    {
        alignas(64) std::uint32_t in[16][16], out[16][16], ref[16][16];
        bool ok = true;
        for (int seed = 0; seed < 64 && ok; ++seed)
        {
            uint64_t s = 0xABCDEF01ull + uint64_t(seed) * 104729ull;
            FillRandom(&in[0][0], 16 * 16, s);
            TransposeScalar(&in[0][0], &ref[0][0], 16, 16);
            std::memset(out, 0xCC, sizeof out);
            Transpose16x16(in, out);
            ok = std::memcmp(out, ref, sizeof out) == 0;
        }
        Report("Transpose16x16 (memory) vs scalar, 64 random seeds", ok);

        // Register form through the same randomized inputs.
        ok = true;
        for (int seed = 0; seed < 64 && ok; ++seed)
        {
            uint64_t s = 0x55AA3C5Cull + uint64_t(seed) * 15485863ull;
            FillRandom(&in[0][0], 16 * 16, s);
            TransposeScalar(&in[0][0], &ref[0][0], 16, 16);
            __m512 r[16], o[16];
            for (int i = 0; i < 16; ++i)
                r[i] = _mm512_castsi512_ps(_mm512_load_si512(&in[i][0]));
            Transpose16x16Reg(r, o);
            std::memset(out, 0xCC, sizeof out);
            for (int i = 0; i < 16; ++i)
                _mm512_store_si512(&out[i][0], _mm512_castps_si512(o[i]));
            ok = std::memcmp(out, ref, sizeof out) == 0;
        }
        Report("Transpose16x16Reg (register) vs scalar, 64 random seeds", ok);

        // Transposing twice must round-trip: the kernel is its own inverse.
        {
            alignas(64) std::uint32_t back[16][16];
            uint64_t s = 0xFEEDFACEull;
            FillRandom(&in[0][0], 16 * 16, s);
            Transpose16x16(in, out);
            Transpose16x16(out, back);
            Report("Transpose16x16 applied twice is identity",
                   std::memcmp(back, in, sizeof in) == 0);
        }

        // Transpose16x8: documented contract is per-lane packing,
        // out_flat[lane*8 + w] == in[w][lane].
        {
            alignas(64) std::uint32_t in8[8][16], out8[8][16], ref[16][8];
            bool ok8 = true;
            for (int seed = 0; seed < 64 && ok8; ++seed)
            {
                uint64_t s = 0x0BADC0DEull + uint64_t(seed) * 911ull;
                FillRandom(&in8[0][0], 8 * 16, s);
                TransposeScalar(&in8[0][0], &ref[0][0], 8, 16);
                std::memset(out8, 0xCC, sizeof out8);
                Transpose16x8(in8, out8);
                ok8 = std::memcmp(out8, ref, sizeof ref) == 0;
            }
            Report("Transpose16x8 per-lane pack vs scalar, 64 random seeds", ok8);
        }
    }
#else
    std::printf("  skip: AVX-512 kernels (not compiled for this host)\n");
#endif

#if defined(__AVX2__)
    Report("Transpose8x8 vs scalar, 64 random seeds",
           CheckSquare([](const std::uint32_t (&in)[8][8],
                          std::uint32_t (&out)[8][8]) { Transpose8x8(in, out); }));
#else
    std::printf("  skip: Transpose8x8 (no AVX2)\n");
#endif

    if (g_failures)
    {
        std::fprintf(stderr, "%d transpose check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("transpose checks passed\n");
    return 0;
}
