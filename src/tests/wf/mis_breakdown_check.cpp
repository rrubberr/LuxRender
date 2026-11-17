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

// MIS denominator breakdown self-consistency (no rendering).
//
// WeightPathV returns the MIS denominator D = sum_j (p_j / p_s)^k over every
// strategy j that could generate the joined path; the connector weight is
// w = 1/D. The Breakdown out-param splits D into its six lobes:
//
//     base + directOrigin + hitLightAlt + lightExt + eyeExt + directPath == D
//
// Each lobe must be counted exactly once. A lobe double-counted (or dropped)
// makes w too small (darkening) or too large (the firefly / over-bright
// symptom), so this equality is the load-bearing invariant behind the
// weight-sum-to-1 property. It is checked here against hand-built paths with
// known densities, across the strategy shapes that the connectors emit:
// Gen (nE>=2, nL>=2), Nee (nL==1 direct), CamNee (nE==1, nL==1) and the
// HitLight pass-through (nL==0).

#include "integrators/wf/mis.h"
#include "integrators/wf/storage.h"
#include "integrators/wf/types.h"

#include <cmath>
#include <cstdint>
#include <iostream>

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

float Lane0(const FloatP &v)
{
    alignas(64) float a[PACKET_WIDTH];
    enoki::store_unaligned(a, v);
    return a[0];
}

// Store an eye MIS record with rr/rrR neutral (1) so RR factors never divide
// a zero word. bits=0 marks the vertex non-specular so lobes stay live.
void StoreEyeMis(PathStore &ps, const UInt32P &rec, float dAW, float dARW,
                 const MaskP &m)
{
    VertexMisP mv;
    mv.dAW = FloatP(dAW);
    mv.dARW = FloatP(dARW);
    mv.rr = FloatP(1.f);
    mv.rrR = FloatP(1.f);
    mv.cosi = FloatP(1.f);
    mv.coso = FloatP(1.f);
    mv.d2 = FloatP(1.f);
    mv.bits = UInt32P(0u);
    StoreMis(ps.arena->misWords.data(), mv, rec, m);
}

// Sum of the six Breakdown lobes.
float BucketSum(const Breakdown &b)
{
    return Lane0(b.base) + Lane0(b.directOrigin) + Lane0(b.hitLightAlt) +
           Lane0(b.lightExt) + Lane0(b.eyeExt) + Lane0(b.directPath);
}

// Assert D == sum(buckets) and w = 1/D in (0, 1].
void CheckSplit(const char *name, const FloatP &Dret, const Breakdown &b)
{
    const float D = Lane0(Dret);
    const float sum = BucketSum(b);
    const float tol = 1e-4f * std::max(1.f, std::fabs(D));
    Check(std::fabs(D - sum) <= tol, name);
    const float w = 1.f / D;
    Check(w > 0.f && w <= 1.f + 1e-5f, "weight in (0,1]");
}

// Gen-style connection: eye PoolView (nE=3) x light PoolView (nL=2). Both
// extensions run, so lightExt and eyeExt lobes are exercised together.
void TestGen(PathStore &eyePs, PathStore &lightPs, const WalkCtx &ctx)
{
    const UInt32P ePath(0u), lPath(0u);
    const MaskP all = true;
    // Eye vertices 0..2.
    StoreEyeMis(eyePs, RecIdx(eyePs, ePath, Int32P(0), all), 0.f, 0.f, all);
    StoreEyeMis(eyePs, RecIdx(eyePs, ePath, Int32P(1), all), 0.6f, 0.7f, all);
    StoreEyeMis(eyePs, RecIdx(eyePs, ePath, Int32P(2), all), 0.4f, 0.5f, all);
    // Light vertices 0..1.
    StoreEyeMis(lightPs, RecIdx(lightPs, lPath, Int32P(0), all), 0.9f, 0.8f,
                all);
    StoreEyeMis(lightPs, RecIdx(lightPs, lPath, Int32P(1), all), 0.3f, 0.35f,
                all);

    Transient tr;
    tr.eAF_last = FloatP(0.5f);
    tr.eAR_last = FloatP(0.4f);
    tr.eAR_prev = FloatP(0.3f);
    tr.lAR_last = FloatP(0.3f);
    tr.lAR_prev = FloatP(0.25f);
    tr.pdfLightDirect = FloatP(0.2f);
    tr.isLightDirect = false;

    const PoolView eyeV{&eyePs, ePath};
    const PoolView lightV{&lightPs, lPath};
    Breakdown bd;
    const FloatP D =
        WeightPathV(ctx, eyeV, Int32P(3), lightV, Int32P(2), tr, all, &bd);
    CheckSplit("Gen nE=3,nL=2 split sums to D", D, bd);
}

// Nee-style: eye PoolView (nE=3) x Virtual1 light (nL==1, direct). Exercises
// the directOrigin lobe and the eye-extension toward a single light vertex.
void TestNee(PathStore &eyePs, const WalkCtx &ctx)
{
    const UInt32P ePath(0u);
    const MaskP all = true;
    StoreEyeMis(eyePs, RecIdx(eyePs, ePath, Int32P(0), all), 0.f, 0.f, all);
    StoreEyeMis(eyePs, RecIdx(eyePs, ePath, Int32P(1), all), 0.6f, 0.7f, all);
    StoreEyeMis(eyePs, RecIdx(eyePs, ePath, Int32P(2), all), 0.4f, 0.5f, all);

    Transient tr;
    tr.eAF_last = FloatP(0.5f);
    tr.eAR_last = FloatP(0.4f);
    tr.eAR_prev = FloatP(0.3f);
    tr.lAR_last = FloatP(0.3f);
    tr.pdfLightDirect = FloatP(0.2f);
    tr.isLightDirect = true;

    const PoolView eyeV{&eyePs, ePath};
    const Virtual1 lightV{FloatP(0.6f)};
    Breakdown bd;
    const FloatP D =
        WeightPathV(ctx, eyeV, Int32P(3), lightV, Int32P(1), tr, all, &bd);
    CheckSplit("Nee nE=3,nL=1 split sums to D", D, bd);
}

// CamNee-style: NullView eye (nE==1) x Virtual1 light (nL==1). The smallest
// connection; only base + directOrigin should be nonzero.
void TestCamNee(const WalkCtx &ctx)
{
    Transient tr;
    tr.eAF_last = FloatP(0.5f);
    tr.eAR_last = FloatP(0.4f);
    tr.lAR_last = FloatP(0.3f);
    tr.pdfLightDirect = FloatP(0.2f);
    tr.isLightDirect = true;

    const NullView eyeV;
    const Virtual1 lightV{FloatP(0.6f)};
    Breakdown bd;
    const FloatP D =
        WeightPathV(ctx, eyeV, Int32P(1), lightV, Int32P(1), tr, MaskP(true),
                    &bd);
    CheckSplit("CamNee nE=1,nL=1 split sums to D", D, bd);
}

// HitLight pass-through: eye PoolView (nE=3) x NullView light (nL==0).
// Exercises the hitLightAlt lobe.
void TestHitLight(PathStore &eyePs, const WalkCtx &ctx)
{
    const UInt32P ePath(0u);
    const MaskP all = true;
    StoreEyeMis(eyePs, RecIdx(eyePs, ePath, Int32P(0), all), 0.f, 0.f, all);
    StoreEyeMis(eyePs, RecIdx(eyePs, ePath, Int32P(1), all), 0.6f, 0.7f, all);
    StoreEyeMis(eyePs, RecIdx(eyePs, ePath, Int32P(2), all), 0.4f, 0.5f, all);

    Transient tr;
    tr.eAF_last = FloatP(0.5f);
    tr.eAR_last = FloatP(0.4f);
    tr.eAR_prev = FloatP(0.3f);
    tr.pdfLightDirect = FloatP(0.2f);
    tr.isLightDirect = false;

    const PoolView eyeV{&eyePs, ePath};
    const NullView lightV;
    Breakdown bd;
    const FloatP D =
        WeightPathV(ctx, eyeV, Int32P(3), lightV, Int32P(0), tr, all, &bd);
    CheckSplit("HitLight nE=3,nL=0 split sums to D", D, bd);
}
} // namespace

int main()
{
    std::cout << "lux2 wf mis_breakdown_check (denominator lobe split)"
              << std::endl;

    WalkCtx ctx;
    ctx.eyeDepth = 8;
    ctx.lightDepth = 8;
    ctx.cfg.misPower = 2.f;
    ctx.cfg.noMis = false;

    BlockArena eyeArena, lightArena;
    PathStore eyePs, lightPs;
    eyePs.Init(&eyeArena, 4);
    lightPs.Init(&lightArena, 4);

    TestGen(eyePs, lightPs, ctx);
    TestNee(eyePs, ctx);
    TestCamNee(ctx);
    TestHitLight(eyePs, ctx);

    if (g_failures == 0)
        std::cout << "MIS breakdown split self-consistency passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
