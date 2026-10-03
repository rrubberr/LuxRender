/***************************************************************************
 *   This file is part of LuxRender.                                       *
 *                                                                         *
 *   To the extent possible under law, the author(s) have dedicated all    *
 *   copyright and related neighboring rights to the belowe code to the    *
 *   public domain worldwide. The below code is distributed without any    *
 *   warranty.                                                             *
 *                                                                         *
 *   See: <https://creativecommons.org/publicdomain/zero/1.0/>             *
 *                                                                         *
 ***************************************************************************/
// Tests are machine generated. Proceed with caution!

// Verifies the LDSampler.

#include "samplers/ldsampler.h"
#include "core/vecp.h"

#include <cmath>
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

} // namespace

int main() {
    std::cout << "lux2 sampler_check (Stage 2)" << std::endl;

    // ---- 1. square power-of-two rounding --------------------------------
    {
        Check(LDSampler(1, 0).SampleCount() == 4, "spp 1 -> 4");
        Check(LDSampler(4, 0).SampleCount() == 4, "spp 4 -> 4");
        Check(LDSampler(5, 0).SampleCount() == 16, "spp 5 -> 16");
        Check(LDSampler(16, 0).SampleCount() == 16, "spp 16 -> 16");
        Check(LDSampler(17, 0).SampleCount() == 64, "spp 17 -> 64");
    }

    // ---- 2. range -------------------------------------------------------
    {
        LDSampler s(16, 42);
        s.Seed(0, PACKET_WIDTH);
        bool inRange = true;
        for (uint32_t i = 0; i < 16; ++i) {
            const FloatP v = s.Next1D();
            const Point2fP p = s.Next2D();
            for (size_t l = 0; l < PACKET_WIDTH; ++l) {
                if (!(v[l] >= 0.f && v[l] < 1.f)) inRange = false;
                if (!(p.x()[l] >= 0.f && p.x()[l] < 1.f)) inRange = false;
                if (!(p.y()[l] >= 0.f && p.y()[l] < 1.f)) inRange = false;
            }
            s.Advance();
        }
        Check(inRange, "Next1D/Next2D values in [0,1)");
    }

    // ---- 3. 1D stratification (lane 0, one sequence) --------------------
    {
        constexpr uint32_t spp = 64;
        LDSampler s(spp, 7);
        s.Seed(0, PACKET_WIDTH);
        std::vector<int> bins(spp, 0);
        for (uint32_t i = 0; i < spp; ++i) {
            const FloatP v = s.Next1D();
            const int b = int(float(v[0]) * float(spp));
            if (b >= 0 && b < int(spp)) bins[b]++;
            s.Advance();
        }
        bool perfect = true;
        for (uint32_t i = 0; i < spp; ++i)
            if (bins[i] != 1) perfect = false;
        Check(perfect, "1D stratification perfect over spp draws");
    }

    // ---- 4. reproducibility + decorrelation -----------------------------
    {
        LDSampler a(16, 123), b(16, 123);
        a.Seed(0, PACKET_WIDTH);
        b.Seed(0, PACKET_WIDTH);
        bool repro = true;
        for (uint32_t i = 0; i < 16; ++i) {
            const FloatP va = a.Next1D(), vb = b.Next1D();
            for (size_t l = 0; l < PACKET_WIDTH; ++l)
                if (float(va[l]) != float(vb[l])) repro = false;
            a.Advance(); b.Advance();
        }
        Check(repro, "same base seed reproduces stream");

        LDSampler c(16, 123);
        c.Seed(999, PACKET_WIDTH);   // different seedOffset
        a.Seed(0, PACKET_WIDTH);
        bool differ = false;
        const FloatP va = a.Next1D(), vc = c.Next1D();
        for (size_t l = 0; l < PACKET_WIDTH; ++l)
            if (float(va[l]) != float(vc[l])) differ = true;
        Check(differ, "different seedOffset decorrelates");
    }

    // ---- 5. lanes distinct ----------------------------------------------
    {
        LDSampler s(16, 5);
        s.Seed(0, PACKET_WIDTH);
        const FloatP v = s.Next1D();
        bool lanesDiffer = false;
        for (size_t l = 1; l < PACKET_WIDTH; ++l)
            if (float(v[l]) != float(v[0])) lanesDiffer = true;
        Check(lanesDiffer, "lanes (sequences) produce distinct values");
    }

    std::cout << (g_failures == 0 ? "ALL PASSED" : "FAILURES")
              << " (" << g_failures << ")" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
