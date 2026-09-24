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

// Verifies the QMC substrate.

#include "core/qmc.h"
#include "core/vecp.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

using namespace lux2;

namespace {

int g_failures = 0;

void Check(bool cond, const char *what) {
    if (!cond) {
        std::cerr << "  FAIL: " << what << std::endl;
        ++g_failures;
    } else {
        std::cout << "  ok:   " << what << std::endl;
    }
}

// Scalar reference: bit-reverse a 32-bit index and map into [0,1).
float ScalarRadicalInverse2(uint32_t index, uint32_t scramble) {
    uint32_t r = index;
    r = (r << 16) | (r >> 16);
    r = ((r & 0x00ff00ffu) << 8) | ((r & 0xff00ff00u) >> 8);
    r = ((r & 0x0f0f0f0fu) << 4) | ((r & 0xf0f0f0f0u) >> 4);
    r = ((r & 0x33333333u) << 2) | ((r & 0xccccccccu) >> 2);
    r = ((r & 0x55555555u) << 1) | ((r & 0xaaaaaaaau) >> 1);
    r = (r ^ scramble) >> 9;
    r |= 0x3f800000u;
    float f;
    std::memcpy(&f, &r, sizeof(f));
    return f - 1.f;
}

} // namespace

int main() {
    std::cout << "lux2 qmc_check (Stage 0)" << std::endl;

    // ---- 1. radical_inverse_2 vs scalar reference -----------------------
    {
        // Fill a packet with indices 0..PACKET_WIDTH-1, scramble 0.
        UInt32P idx;
        for (size_t i = 0; i < PACKET_WIDTH; ++i) idx[i] = uint32_t(i);
        const FloatP v = radical_inverse_2(idx, UInt32P(0u));

        bool allMatch = true;
        bool inRange = true;
        for (size_t i = 0; i < PACKET_WIDTH; ++i) {
            const float ref = ScalarRadicalInverse2(uint32_t(i), 0u);
            if (std::abs(float(v[i]) - ref) > 1e-7f) allMatch = false;
            if (!(v[i] >= 0.f && v[i] < 1.f)) inRange = false;
        }
        Check(allMatch, "radical_inverse_2 matches scalar reference");
        Check(inRange, "radical_inverse_2 values in [0,1)");

        // Known value: radical_inverse_2(1,0) == 0.5.
        UInt32P one(1u);
        const FloatP v1 = radical_inverse_2(one, UInt32P(0u));
        Check(std::abs(float(v1[0]) - 0.5f) < 1e-7f,
              "radical_inverse_2(1) == 0.5");
    }

    // ---- 2. sobol_2 in range, distinct from radical_inverse_2 -----------
    {
        UInt32P idx;
        for (size_t i = 0; i < PACKET_WIDTH; ++i) idx[i] = uint32_t(i);
        const FloatP s = sobol_2(idx, UInt32P(0u));
        const FloatP r = radical_inverse_2(idx, UInt32P(0u));

        bool inRange = true;
        for (size_t i = 0; i < PACKET_WIDTH; ++i)
            if (!(s[i] >= 0.f && s[i] < 1.f)) inRange = false;
        Check(inRange, "sobol_2 values in [0,1)");

        // sobol_2(1,0) == 0.5 too, but sobol_2(2,0) == 0.75 vs radical 0.25.
        UInt32P two(2u);
        const FloatP s2 = sobol_2(two, UInt32P(0u));
        const FloatP r2 = radical_inverse_2(two, UInt32P(0u));
        Check(std::abs(float(s2[0]) - 0.75f) < 1e-6f, "sobol_2(2) == 0.75");
        Check(std::abs(float(r2[0]) - 0.25f) < 1e-6f, "radical_inverse_2(2) == 0.25");
    }

    // ---- 3. permute is a bijection on [0, n) ----------------------------
    {
        constexpr uint32_t n = 16;
        // Permute indices 0..n-1 across successive packets and collect.
        std::vector<int> seen(n, 0);
        bool ok = true;
        for (uint32_t base = 0; base < n; base += PACKET_WIDTH) {
            UInt32P idx;
            for (size_t i = 0; i < PACKET_WIDTH; ++i)
                idx[i] = base + uint32_t(i);
            const UInt32P p = permute(idx, n, UInt32P(0x1234u));
            for (size_t i = 0; i < PACKET_WIDTH; ++i) {
                const uint32_t v = p[i];
                if (v >= n) { ok = false; continue; }
                seen[v]++;
            }
        }
        bool bijective = ok;
        for (uint32_t i = 0; i < n; ++i)
            if (seen[i] != 1) bijective = false;
        Check(bijective, "permute is a bijection on [0,16)");
    }

    // ---- 4. sample_tea_32 deterministic + lane-varying ------------------
    {
        UInt32P a(0xdeadbeefu), b(0x12345678u);
        const UInt32P h1 = sample_tea_32(a, b);
        const UInt32P h2 = sample_tea_32(a, b);
        bool deterministic = true;
        for (size_t i = 0; i < PACKET_WIDTH; ++i)
            if (h1[i] != h2[i]) deterministic = false;
        Check(deterministic, "sample_tea_32 deterministic");

        // Different inputs -> different output.
        const UInt32P h3 = sample_tea_32(a, UInt32P(0x12345679u));
        bool differs = false;
        for (size_t i = 0; i < PACKET_WIDTH; ++i)
            if (h1[i] != h3[i]) differs = true;
        Check(differs, "sample_tea_32 varies with input");
    }

    std::cout << (g_failures == 0 ? "ALL PASSED" : "FAILURES")
              << " (" << g_failures << ")" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
