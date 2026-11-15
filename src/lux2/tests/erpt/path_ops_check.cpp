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

// ERPT path_ops self-consistency (no rendering, no scene).
//
// Validates the arena bookkeeping that a Metropolis chain relies on:
//   - CopyVertex moves both the geom and MIS records of one vertex.
//   - ClonePath deep-copies [0,n) plus the per-path metadata a connector
//     reads (lightIdx / directPdf1 / selPdf / owner / lightSlot / n).
//   - TruncatePath shortens n without disturbing records below the cut, so a
//     later re-walk from the cut point overwrites cleanly.
//
// The evaluator's numeric parity against the connector (f*rrProd == Lc*p_s)
// needs real BSDFs and occlusion, so per this repo's convention it lands in
// the render check; here we only exercise the pure storage moves with
// synthetic records, mirroring wf/mis_weight_check.cpp.

#include "integrators/erpt/path_ops.h"
#include "integrators/wf/storage.h"

#include "core/math/ray.h"

#include <cstdint>
#include <iostream>

using namespace lux2;
using namespace lux2::wf;
using namespace lux2::erpt;

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

// Write a recognisable geom+mis record at (path,v): every field encodes
// (path*100 + v) so a copy that reads the wrong source is caught.
void StoreMarker(PathStore &ps, uint32_t path, uint32_t v)
{
    const MaskP all = true;
    const UInt32P rec = RecIdx(ps, UInt32P(path), Int32P(int(v)), all);
    const float tag = float(path * 100u + v);

    VertexGeomP g;
    g.p = Point3fP(tag, tag + 1.f, tag + 2.f);
    g.n = Normal3fP(0.f);
    g.ng = Normal3fP(0.f);
    g.wb = Vector3fP(tag, 0.f, 0.f);
    g.dp_du = Vector3fP(0.f);
    g.dp_dv = Vector3fP(0.f);
    g.dp_ds = Vector3fP(0.f);
    g.dp_dt = Vector3fP(0.f);
    g.uv = Point2fP(tag, tag);
    g.flux = SWCSpectrumP(tag);
    g.k = FloatP(0.f);
    g.footprint = FloatP(0.f);
    g.spare0 = FloatP(0.f);
    g.matID = UInt32P(path * 10u + v);
    g.medID = UInt32P(lux2::LUX2_MEDIUM_CLEAR);
    g.medFront = UInt32P(lux2::LUX2_MEDIUM_CLEAR);
    g.medBack = UInt32P(lux2::LUX2_MEDIUM_CLEAR);
    StoreGeom(ps.arena->geomWords.data(), g, rec, all);

    VertexMisP mv;
    mv.dAW = FloatP(tag + 3.f);
    mv.dARW = FloatP(tag + 4.f);
    mv.rr = FloatP(1.f);
    mv.rrR = FloatP(1.f);
    mv.cosi = FloatP(tag + 5.f);
    mv.coso = FloatP(tag + 6.f);
    mv.d2 = FloatP(tag + 7.f);
    mv.bits = UInt32P(0u);
    StoreMis(ps.arena->misWords.data(), mv, rec, all);
}

// Read back the geom p.x and mis dAW at (path,v) to compare against a marker.
void LoadMarker(const PathStore &ps, uint32_t path, uint32_t v, float *px,
                float *dAW)
{
    const MaskP all = true;
    const UInt32P rec = RecIdx(ps, UInt32P(path), Int32P(int(v)), all);
    const VertexGeomP g = LoadGeom(ps.arena->geomWords.data(), rec, all);
    const VertexMisP mv = LoadMis(ps.arena->misWords.data(), rec, all);
    *px = Lane0(g.p.x());
    *dAW = Lane0(mv.dAW);
}

bool MarkerMatches(const PathStore &ps, uint32_t path, uint32_t v)
{
    float px = 0.f, dAW = 0.f;
    LoadMarker(ps, path, v, &px, &dAW);
    const float tag = float(path * 100u + v);
    return px == tag && dAW == tag + 3.f;
}
} // namespace

int main()
{
    std::cout << "lux2 mut path_ops_check (clone / truncate / copy)" << std::endl;

    BlockArena arena;
    PathStore ps;
    ps.Init(&arena, 8);
    const MaskP all = true;

    // ---- 1. CopyVertex moves geom AND mis ------------------------------
    {
        StoreMarker(ps, /*src*/ 1u, /*v*/ 2u);
        CopyVertex(ps, /*dst*/ 5u, ps, /*src*/ 1u, /*v*/ 2u);
        // dst now carries the src tag (1*100+2 = 102), read at path 5.
        float px = 0.f, dAW = 0.f;
        LoadMarker(ps, 5u, 2u, &px, &dAW);
        Check(px == 102.f && dAW == 105.f,
              "CopyVertex moves geom.p and mis.dAW together");
    }

    // ---- 2. ClonePath copies [0,n) + metadata --------------------------
    {
        const uint32_t src = 2u, dst = 6u;
        for (uint32_t v = 0; v < 3; ++v)
            StoreMarker(ps, src, v);
        ps.n[src] = 3u;
        ps.lightIdx[src] = 7u;
        ps.directPdf1[src] = 0.25f;
        ps.selPdf[src] = 0.5f;
        ps.owner[src] = 42u;
        ps.lightSlot[src] = 9u;

        ClonePath(ps, dst, ps, src);

        Check(ps.n[dst] == 3u, "ClonePath copies vertex count n");
        Check(ps.lightIdx[dst] == 7u && ps.directPdf1[dst] == 0.25f &&
                  ps.selPdf[dst] == 0.5f && ps.owner[dst] == 42u &&
                  ps.lightSlot[dst] == 9u,
              "ClonePath copies connector metadata");
        bool vertsOk = true;
        for (uint32_t v = 0; v < 3; ++v)
        {
            float px = 0.f, dAW = 0.f;
            LoadMarker(ps, dst, v, &px, &dAW);
            const float tag = float(src * 100u + v); // src tag, not dst
            if (px != tag || dAW != tag + 3.f)
                vertsOk = false;
        }
        Check(vertsOk, "ClonePath deep-copies each vertex record (src tags)");
    }

    // ---- 3. TruncatePath shortens n, leaves lower records intact -------
    {
        const uint32_t p = 3u;
        for (uint32_t v = 0; v < 4; ++v)
            StoreMarker(ps, p, v);
        ps.n[p] = 4u;

        TruncatePath(ps, p, /*k*/ 2u);
        Check(ps.n[p] == 2u, "TruncatePath sets n to k");
        // Records below the cut must be untouched (re-walk overwrites >= k).
        Check(MarkerMatches(ps, p, 0u) && MarkerMatches(ps, p, 1u),
              "TruncatePath leaves [0,k) records intact");
        // Truncating to a larger k is a no-op (never grows).
        TruncatePath(ps, p, /*k*/ 5u);
        Check(ps.n[p] == 2u, "TruncatePath never grows n");
    }

    // ---- 4. CopySpan copies [lo,hi] only, clamped to n -----------------
    {
        const uint32_t src = 4u, dst = 7u;
        for (uint32_t v = 0; v < 5; ++v)
            StoreMarker(ps, src, v);
        ps.n[src] = 5u;
        // Pre-poison dst vertices 1..3 so we can see which got overwritten.
        for (uint32_t v = 1; v <= 3; ++v)
            StoreMarker(ps, dst, v);

        CopySpan(ps, dst, ps, src, /*lo*/ 1u, /*hi*/ 2u);
        // Only [1,2] should now carry src tags; 3 stays poisoned (dst tag).
        float p1 = 0.f, d1 = 0.f, p2 = 0.f, d2 = 0.f, p3 = 0.f, d3 = 0.f;
        LoadMarker(ps, dst, 1u, &p1, &d1);
        LoadMarker(ps, dst, 2u, &p2, &d2);
        LoadMarker(ps, dst, 3u, &p3, &d3);
        Check(p1 == 401.f && p2 == 402.f, "CopySpan copies [lo,hi]");
        Check(p3 == 703.f, "CopySpan leaves vertices outside [lo,hi] alone");

        // hi beyond n is clamped: copying [3,99] writes only vertex 4 (n=5).
        StoreMarker(ps, dst, 4u); // poison
        CopySpan(ps, dst, ps, src, /*lo*/ 3u, /*hi*/ 99u);
        float p4 = 0.f, d4 = 0.f;
        LoadMarker(ps, dst, 4u, &p4, &d4);
        Check(p4 == 404.f, "CopySpan clamps hi to n-1 (copies vertex 4)");
    }

    // ---- 5. Self-copy (identical src/dst) is safe (memmove) ------------
    {
        const uint32_t p = 5u;
        StoreMarker(ps, p, 0u);
        // Copying a record onto itself must leave it intact and not crash.
        CopyVertex(ps, p, ps, p, 0u);
        Check(MarkerMatches(ps, p, 0u), "CopyVertex self-copy is a safe no-op");
    }

    if (g_failures == 0)
        std::cout << "path_ops self-consistency passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
