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

// Phase 5 foundation: the store-walker lane tails blend correctly and the
// sample table's conflict-safe accumulate matches a scalar reference.

#include "integrators/wf/lanes.h"
#include "integrators/wf/sample_table.h"

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
    else
    {
        std::cout << "  ok:   " << what << std::endl;
    }
}
} // namespace

int main()
{
    std::cout << "lux2 wf lanes_check (Phase 5 foundation)" << std::endl;

    // Blend keeps `a` where the mask is set, `b` elsewhere -- per field.
    {
        EyeLanes a = EyeLanes::Fresh(PixelPlan{});
        EyeLanes b = EyeLanes::Fresh(PixelPlan{});
        a.v = Int32P(7);
        b.v = Int32P(3);
        a.prevD2 = FloatP(11.f);
        b.prevD2 = FloatP(22.f);
        const MaskP m = (enoki::arange<Int32P>() & Int32P(1)) == Int32P(0);
        const EyeLanes r = Blend(m, a, b);
        std::vector<int32_t> rv(PACKET_WIDTH);
        std::vector<float> dv(PACKET_WIDTH), mv(PACKET_WIDTH);
        enoki::store_unaligned(rv.data(), r.v);
        enoki::store_unaligned(dv.data(), r.prevD2);
        enoki::store_unaligned(mv.data(), select(m, FloatP(1.f), FloatP(0.f)));
        bool ok = true;
        for (size_t i = 0; i < PACKET_WIDTH; ++i)
        {
            const int32_t ev = mv[i] > 0.f ? 7 : 3;
            const float ed = mv[i] > 0.f ? 11.f : 22.f;
            if (rv[i] != ev || dv[i] != ed)
                ok = false;
        }
        Check(ok, "EyeLanes::Blend selects per field");
    }

    // AddToSample: several lanes onto the same slot accumulate (conflict-safe).
    {
        std::vector<float> L(4, 0.f);
        uint32_t si[PACKET_WIDTH];
        float xv[PACKET_WIDTH];
        for (size_t i = 0; i < PACKET_WIDTH; ++i)
        {
            si[i] = uint32_t(i % 4); // collisions across lanes
            xv[i] = 1.f;
        }
        const UInt32P s = enoki::load_unaligned<UInt32P>(si);
        const FloatP x = enoki::load_unaligned<FloatP>(xv);
        AddToSample(L.data(), s, x, MaskP(true));
        // PACKET_WIDTH lanes spread over 4 slots -> each slot gets PW/4.
        const float per = float(PACKET_WIDTH) / 4.f;
        bool ok = true;
        for (float v : L)
            if (v != per)
                ok = false;
        Check(ok, "AddToSample accumulates colliding lanes");
    }

    // AddToSample respects the mask.
    {
        std::vector<float> L(4, 0.f);
        uint32_t si[PACKET_WIDTH];
        float xv[PACKET_WIDTH];
        for (size_t i = 0; i < PACKET_WIDTH; ++i)
        {
            si[i] = uint32_t(i % 4);
            xv[i] = 1.f;
        }
        const UInt32P s = enoki::load_unaligned<UInt32P>(si);
        const FloatP x = enoki::load_unaligned<FloatP>(xv);
        const MaskP m = enoki::arange<Int32P>() < Int32P(4); // first 4 lanes
        AddToSample(L.data(), s, x, m);
        // First 4 lanes hit slots 0..3 once each.
        bool ok = true;
        for (float v : L)
            if (v != 1.f)
                ok = false;
        Check(ok, "AddToSample honours the mask");
    }

    if (g_failures == 0)
        std::cout << "lanes checks passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
