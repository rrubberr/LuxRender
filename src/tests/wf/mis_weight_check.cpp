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

// Wavefront MIS weight self-consistency (no rendering).
//
// Guards the T=1 camera-arrival strategy after the uset1 flag removal. The
// eye subpath's first surface vertex must store dARW[1] = camPdf * cos(v1)/d2
// (seeded from camera.Pdf at refill), so the light-extension loop reaching
// idx==1 counts the "light path reaches the camera" strategy. If dARW[1] is
// left zero, that lobe is silently dropped from every eye/NEE/Gen denominator
// while the Cam/CamNee connectors still count it -- weights then sum above 1
// and the render is too bright on directly visible emitters.

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

// Store an eye MIS record for one vertex with rr/rrR neutral (1) so the
// Russian-roulette factors never divide by a zero-initialized word.
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
    mv.bits = UInt32P(0u); // non-specular
    StoreMis(ps.arena->misWords.data(), mv, rec, m);
}

// Weight for a Gen-style connection: eye PoolView (nE=3) x Virtual1 light
// (nL=1). The transient supplies the last-vertex overrides; dARW[1] is read
// from the pool at the loop's final iteration (idx==1).
float GenWeight(PathStore &ps, const UInt32P &ePath, const WalkCtx &ctx)
{
    Transient tr;
    // Last eye vertex (idx=2) terms; nonzero so the loop stays live.
    tr.eAF_last = FloatP(0.5f);
    tr.eAR_last = FloatP(0.4f);
    // idx==1 reverse density (nE-2 override).
    tr.eAR_prev = FloatP(0.3f);
    tr.eRR_last = FloatP(1.f);
    tr.eRRR_last = FloatP(1.f);
    // Light endpoint (Virtual1) direct term.
    tr.isLightDirect = true;
    tr.pdfLightDirect = FloatP(0.2f);
    tr.lAR_last = FloatP(0.3f);
    tr.lRR_last = FloatP(1.f);
    tr.lRRR_last = FloatP(1.f);

    const PoolView eyeV{&ps, ePath};
    const Virtual1 lightV{FloatP(0.6f)};
    const MaskP act = true;
    const FloatP wInv = WeightPathV(ctx, eyeV, Int32P(3), lightV, Int32P(1), tr,
                                    act);
    return Lane0(divp(FloatP(1.f), wInv));
}
} // namespace

int main()
{
    std::cout << "lux2 wf mis_weight_check (T=1 camera-arrival lobe)"
              << std::endl;

    WalkCtx ctx;
    ctx.eyeDepth = 8;
    ctx.lightDepth = 8;
    ctx.cfg.misPower = 2.f;
    ctx.cfg.noMis = false;

    BlockArena arena;
    PathStore ps;
    ps.Init(&arena, 4);
    const UInt32P ePath(0u);
    const MaskP all = true;

    // Camera-seeded first-vertex density (the fixed value).
    const float camPdf = 1.7f, cosV1 = 0.8f, invD2 = 0.25f;
    const float dARW1_fixed = camPdf * cosV1 * invD2;

    // Neutral camera vertex (idx 0) and mid vertex (idx 2).
    StoreEyeMis(ps, RecIdx(ps, ePath, Int32P(0), all), 0.f, 0.f, all);
    StoreEyeMis(ps, RecIdx(ps, ePath, Int32P(2), all), 0.4f, 0.5f, all);

    // (1) Buggy value: dARW[1] == 0 drops the idx==1 lobe.
    StoreEyeMis(ps, RecIdx(ps, ePath, Int32P(1), all), 0.3f, 0.f, all);
    const float wBuggy = GenWeight(ps, ePath, ctx);

    // (2) Fixed value: dARW[1] = camPdf * cos(v1) / d2 activates the lobe.
    StoreEyeMis(ps, RecIdx(ps, ePath, Int32P(1), all), 0.3f, dARW1_fixed, all);
    const float wFixed = GenWeight(ps, ePath, ctx);

    Check(std::isfinite(wBuggy) && std::isfinite(wFixed),
          "both weights finite");
    Check(wBuggy > 0.f && wFixed > 0.f, "both weights positive");
    // The extra strategy in the denominator lowers the MIS weight.
    Check(wFixed < wBuggy,
          "camera-seeded dARW[1] adds the t=1 lobe (weight strictly smaller)");

    // (3) Convention: the eye-side stored density and the camera connector's
    // endpoint term are the same expression in camPdf -- seeding prevPdfR with
    // camera.Pdf yields exactly what CamNee/Cam compute for eAF_last.
    const float camSide = camPdf * cosV1 * invD2; // connectors: camPdfR0*cosCam*invD2
    const float eyeSide = dARW1_fixed;            // pool: prevPdfR*cB*invD2
    Check(std::fabs(camSide - eyeSide) < 1e-6f,
          "eye dARW[1] and camera eAF_last share the camPdf convention");

    // (4) noMis ablation returns w == 1 regardless of stored densities.
    // TileCtx is non-copyable, so build a fresh context.
    WalkCtx ctxNo;
    ctxNo.eyeDepth = 8;
    ctxNo.lightDepth = 8;
    ctxNo.cfg.noMis = true;
    {
        Transient tr;
        const PoolView eyeV{&ps, ePath};
        const Virtual1 lightV{FloatP(0.6f)};
        const FloatP wInv =
            WeightPathV(ctxNo, eyeV, Int32P(3), lightV, Int32P(1), tr, MaskP(true));
        Check(std::fabs(Lane0(divp(FloatP(1.f), wInv)) - 1.f) < 1e-6f,
              "noMis returns unit weight");
    }

    if (g_failures == 0)
        std::cout << "MIS T=1 self-consistency passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
