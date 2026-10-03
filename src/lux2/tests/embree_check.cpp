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

// EmbreeScene build + packet intersect/occlude/shade.

#include "core/embree2.h"
#include "core/scene.h"
#include "core/shape.h"
#include "core/ray.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
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

bool Close(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps * (1.f + std::fabs(a) + std::fabs(b));
}

// Scalar lane access (Enoki Packet operator[](size_t) -> coeff).
template <typename P>
auto ex(const P &v, size_t i) { return v[i]; }

// A RayP with all lanes carrying the same scalar values.
RayP BroadcastRay(Point3f o, Vector3f d, float maxt) {
    RayP r;
    r.o = Point3fP(FloatP(o.x()), FloatP(o.y()), FloatP(o.z()));
    r.d = Vector3fP(FloatP(d.x()), FloatP(d.y()), FloatP(d.z()));
    r.UpdateReciprocalDirection();
    r.mint = FloatP(0.f);
    r.maxt = FloatP(maxt);
    r.time = FloatP(0.f);
    r.InitPayload();
    return r;
}

TriangleDesc MakeTri(Point3f v0, Point3f v1, Point3f v2,
                     Normal3f n0, Normal3f n1, Normal3f n2,
                     UV u0, UV u1, UV u2,
                     std::uint32_t mat, std::int32_t light,
                     std::uint32_t group = 0xFFFFFFFFu) {
    TriangleDesc t;
    t.v0 = v0; t.v1 = v1; t.v2 = v2;
    t.n0 = n0; t.n1 = n1; t.n2 = n2;
    t.uv0 = u0; t.uv1 = u1; t.uv2 = u2;
    t.matID = mat; t.lightID = light; t.groupMask = group;
    return t;
}

// Triangle in the z=0 plane spanning [-1,1]^2 (two triangles), facing +z.
MeshDesc MakeGround(std::uint32_t mat, std::int32_t light) {
    MeshDesc m;
    Normal3f up(0.f, 0.f, 1.f);
    m.tris.push_back(MakeTri(
        Point3f(-1, -1, 0), Point3f(1, -1, 0), Point3f(1, 1, 0),
        up, up, up, UV(0, 0), UV(1, 0), UV(1, 1), mat, light));
    m.tris.push_back(MakeTri(
        Point3f(-1, -1, 0), Point3f(1, 1, 0), Point3f(-1, 1, 0),
        up, up, up, UV(0, 0), UV(1, 1), UV(0, 1), mat, light));
    m.bound = BBox(Point3f(-1, -1, 0), Point3f(1, 1, 0));
    return m;
}

// Single triangle, frontal hit.
void CheckFrontalHit() {
    std::cout << "[frontal hit]" << std::endl;
    EmbreeScene es;
    std::vector<MeshDesc> meshes;
    meshes.push_back(MakeGround(7, 3));
    es.BuildFromMeshes(meshes);

    RayP ray = BroadcastRay(Point3f(0, 0, 1), Vector3f(0, 0, -1), 100.f);
    HitP hit;
    es.Intersect(ray, hit, Coherent::Yes);

    Check(enoki::all(hit.hit), "all lanes hit");
    Check(Close(ex(hit.t, 0), 1.f), "t == 1");
    Check(Close(ex(hit.p.z(), 0), 0.f), "p.z == 0");
    Check(Close(ex(hit.p.x(), 0), 0.f), "p.x == 0");
    Check(Close(ex(hit.p.y(), 0), 0.f), "p.y == 0");
    Check(ex(hit.matID, 0) == 7u, "matID round-trip");
    Check(ex(hit.lightID, 0) == 3, "lightID round-trip");
    Check(Close(ex(hit.ngeo.z(), 0), -1.f) ||
          Close(ex(hit.ngeo.z(), 0), 1.f), "ngeo along z");
    Check(Close(ex(hit.sh_n.z(), 0), 1.f), "sh_n == +z");
}

// Miss lanes.
void CheckMissLanes() {
    std::cout << "[miss lanes]" << std::endl;
    EmbreeScene es;
    std::vector<MeshDesc> meshes;
    meshes.push_back(MakeGround(1, -1));
    es.BuildFromMeshes(meshes);

    // Build a packet: even lanes hit, odd lanes miss.
    RayP ray;
    FloatP ox(0.f), oy(0.f), oz(1.f);
    FloatP dx(0.f), dy(0.f), dz(-1.f);
    for (size_t i = 0; i < PACKET_WIDTH; ++i) {
        if (i & 1) {
            ox[i] = 100.f + (float)i;  // off to the side
            dz[i] = 1.f;               // aimed away (+z)
        }
    }
    ray.o = Point3fP(ox, oy, oz);
    ray.d = Vector3fP(dx, dy, dz);
    ray.UpdateReciprocalDirection();
    ray.mint = FloatP(0.f);
    ray.maxt = FloatP(100.f);
    ray.time = FloatP(0.f);
    ray.InitPayload();

    // Kill the last lane entirely.
    MaskP alive(true);
    alive[PACKET_WIDTH - 1] = false;
    ray.alive = alive;

    HitP hit;
    // Prefill for the dead lane.
    hit.t = FloatP(-12345.f);
    hit.hit = MaskP(false);
    es.Intersect(ray, hit, Coherent::No);

    bool ok = true;
    for (size_t i = 0; i < PACKET_WIDTH; ++i) {
        bool expect = alive[i] && !(i & 1);
        if (bool(hit.hit[i]) != expect) ok = false;
    }
    Check(ok, "hit mask matches expected lane pattern");

    // Dead lane untouched.
    Check(Close(ex(hit.t, PACKET_WIDTH - 1), -12345.f),
          "dead lane t untouched");
}

// Occlusion, including ray mask disabling occlusion.
void CheckOcclusion() {
    std::cout << "[occlusion]" << std::endl;
    EmbreeScene es;
    std::vector<MeshDesc> meshes;
    // Ground with a specific group mask (bit 0 only).
    MeshDesc g = MakeGround(1, -1);
    for (auto &t : g.tris) t.groupMask = 0x1u;
    meshes.push_back(g);
    es.BuildFromMeshes(meshes);

    // Ray toward the plane: occluded.
    RayP r1 = BroadcastRay(Point3f(0, 0, 1), Vector3f(0, 0, -1), 100.f);
    MaskP occ = es.Occluded(r1, MaskP(true), Coherent::No);
    Check(enoki::all(occ), "occluded toward plane");

    // Ray with the geometry's group bit cleared: not occluded.
    RayP r2 = BroadcastRay(Point3f(0, 0, 1), Vector3f(0, 0, -1), 100.f);
    r2.mask = UInt32P(0xFFFFFFFEu);  // clear bit 0
    MaskP occ2 = es.Occluded(r2, MaskP(true), Coherent::No);
    Check(!enoki::any(occ2), "ray-mask clears occlusion");

    // Ray aimed away: not occluded.
    RayP r3 = BroadcastRay(Point3f(0, 0, 1), Vector3f(0, 0, 1), 100.f);
    MaskP occ3 = es.Occluded(r3, MaskP(true), Coherent::No);
    Check(!enoki::any(occ3), "aimed away not occluded");
}

// Multi-mesh geomToBase indexing.
void CheckMultiMesh() {
    std::cout << "[multi-mesh geomToBase]" << std::endl;
    EmbreeScene es;
    std::vector<MeshDesc> meshes;
    // Three meshes at distinct z, distinct matID. Each is a small quad
    // (2 tris) centered at origin, at z = 0, -1, -2.
    for (int k = 0; k < 3; ++k) {
        float z = -(float)k;
        MeshDesc m;
        Normal3f up(0, 0, 1);
        float s = 0.2f;
        m.tris.push_back(MakeTri(
            Point3f(-s, -s, z), Point3f(s, -s, z), Point3f(s, s, z),
            up, up, up, UV(0, 0), UV(1, 0), UV(1, 1),
            (std::uint32_t)(10 + k), -1));
        m.tris.push_back(MakeTri(
            Point3f(-s, -s, z), Point3f(s, s, z), Point3f(-s, s, z),
            up, up, up, UV(0, 0), UV(1, 1), UV(0, 1),
            (std::uint32_t)(10 + k), -1));
        m.bound = BBox(Point3f(-s, -s, z), Point3f(s, s, z));
        meshes.push_back(m);
    }
    es.BuildFromMeshes(meshes);

    // Ray from z=+1 downward hits the nearest mesh (k=0, matID 10).
    RayP ray = BroadcastRay(Point3f(0, 0, 1), Vector3f(0, 0, -1), 100.f);
    HitP hit;
    es.Intersect(ray, hit, Coherent::No);
    Check(enoki::all(hit.hit), "multi-mesh hit");
    Check(ex(hit.geomID, 0) == 0u, "geomID == nearest mesh");
    Check(ex(hit.matID, 0) == 10u, "matID via geomToBase");
    Check(Close(ex(hit.t, 0), 1.f), "t to nearest mesh");
}

// 5. Shading interpolation: distinct vertex normals/UVs.
void CheckShading() {
    std::cout << "[shading interpolation]" << std::endl;
    EmbreeScene es;
    std::vector<MeshDesc> meshes;
    MeshDesc m;
    // Single triangle with distinct vertex normals and UVs.
    m.tris.push_back(MakeTri(
        Point3f(0, 0, 0), Point3f(1, 0, 0), Point3f(0, 1, 0),
        Normal3f(0, 0, 1), Normal3f(0, 0, 1), Normal3f(0, 0, 1),
        UV(0, 0), UV(1, 0), UV(0, 1), 5, -1));
    m.bound = BBox(Point3f(0, 0, 0), Point3f(1, 1, 0));
    meshes.push_back(m);
    es.BuildFromMeshes(meshes);

    // Ray through the centroid (1/3, 1/3, 0): uv should be (1/3, 1/3).
    RayP ray = BroadcastRay(Point3f(1.f/3.f, 1.f/3.f, 1.f),
                            Vector3f(0, 0, -1), 100.f);
    HitP hit;
    es.Intersect(ray, hit, Coherent::No);
    Check(enoki::all(hit.hit), "centroid hit");
    Check(Close(ex(hit.uv.x(), 0), 1.f/3.f), "uv.u centroid");
    Check(Close(ex(hit.uv.y(), 0), 1.f/3.f), "uv.v centroid");
    Check(Close(ex(hit.sh_n.z(), 0), 1.f), "sh_n +z");
}

// 6. Degenerate safety: zero-area triangle, no NaNs.
void CheckDegenerate() {
    std::cout << "[degenerate safety]" << std::endl;
    EmbreeScene es;
    std::vector<MeshDesc> meshes;
    MeshDesc m;
    // Zero area triangle (all vertices collinear) with zero vertex normals.
    m.tris.push_back(MakeTri(
        Point3f(0, 0, 0), Point3f(1, 0, 0), Point3f(2, 0, 0),
        Normal3f(0, 0, 0), Normal3f(0, 0, 0), Normal3f(0, 0, 0),
        UV(0, 0), UV(0, 0), UV(0, 0), 2, -1));
    m.bound = BBox(Point3f(0, 0, 0), Point3f(2, 0, 0));
    meshes.push_back(m);
    es.BuildFromMeshes(meshes);

    // A ray near the degenerate triangle; whether it hits or not, no NaNs.
    RayP ray = BroadcastRay(Point3f(1, 0, 1), Vector3f(0, 0, -1), 100.f);
    HitP hit;
    es.Intersect(ray, hit, Coherent::No);

    // Check no NaN in shading outputs on hit lanes.
    bool nan = false;
    for (size_t i = 0; i < PACKET_WIDTH; ++i) {
        if (!hit.hit[i]) continue;
        if (std::isnan(ex(hit.p.x(), i)) ||
            std::isnan(ex(hit.sh_n.x(), i)) ||
            std::isnan(ex(hit.sh_n.y(), i)) ||
            std::isnan(ex(hit.sh_n.z(), i)) ||
            std::isnan(ex(hit.uv.x(), i)))
            nan = true;
    }
    Check(!nan, "no NaNs from degenerate triangle");
}

// Coherent vs incoherent parity.
void CheckCoherencyParity() {
    std::cout << "[coherency parity]" << std::endl;
    EmbreeScene es;
    std::vector<MeshDesc> meshes;
    meshes.push_back(MakeGround(4, -1));
    es.BuildFromMeshes(meshes);

    RayP ra = BroadcastRay(Point3f(0.1f, 0.2f, 1), Vector3f(0, 0, -1), 100.f);
    RayP rb = ra;
    HitP ha, hb;
    es.Intersect(ra, ha, Coherent::Yes);
    es.Intersect(rb, hb, Coherent::No);

    Check(bool(ha.hit[0]) == bool(hb.hit[0]), "parity: hit mask");
    Check(Close(ex(ha.t, 0), ex(hb.t, 0)),
          "parity: t");
    Check(Close(ex(ha.p.x(), 0), ex(hb.p.x(), 0)),
          "parity: p.x");
}

// Scale/perf smoke (non-asserting).
void CheckPerfSmoke() {
    std::cout << "[perf smoke]" << std::endl;
    EmbreeScene es;
    std::vector<MeshDesc> meshes;
    MeshDesc m;
    const int N = 1000;  // N x N grid => 2*N*N triangles (~2M). Keep modest.
    const float step = 1.f / (float)N;
    Normal3f up(0, 0, 1);
    m.tris.reserve((size_t)2 * N * N);
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            float x0 = x * step, x1 = (x + 1) * step;
            float y0 = y * step, y1 = (y + 1) * step;
            m.tris.push_back(MakeTri(
                Point3f(x0, y0, 0), Point3f(x1, y0, 0), Point3f(x1, y1, 0),
                up, up, up, UV(0, 0), UV(1, 0), UV(1, 1), 0, -1));
            m.tris.push_back(MakeTri(
                Point3f(x0, y0, 0), Point3f(x1, y1, 0), Point3f(x0, y1, 0),
                up, up, up, UV(0, 0), UV(1, 1), UV(0, 1), 0, -1));
        }
    }
    m.bound = BBox(Point3f(0, 0, 0), Point3f(1, 1, 0));
    meshes.push_back(m);
    es.BuildFromMeshes(meshes);
    std::cout << "  tris: " << es.TriangleCount() << std::endl;

    // Trace a batch of packets straight down; just confirm they complete.
    size_t packets = 10000;
    size_t hits = 0;
    for (size_t p = 0; p < packets; ++p) {
        float fx = (float)((p * 2654435761u) % 1000u) / 1000.f;
        float fy = (float)((p * 40503u) % 1000u) / 1000.f;
        RayP ray = BroadcastRay(Point3f(fx, fy, 1), Vector3f(0, 0, -1), 100.f);
        HitP hit;
        es.Intersect(ray, hit, Coherent::No);
        if (hit.hit[0]) ++hits;
    }
    std::cout << "  packets: " << packets << ", hits: " << hits << std::endl;
    Check(hits > 0, "perf smoke produced hits");
}

// PACKET_WIDTH tail behavior: only a few alive lanes, no writes to dead.
void CheckTailLanes() {
    std::cout << "[tail lanes]" << std::endl;
    EmbreeScene es;
    std::vector<MeshDesc> meshes;
    meshes.push_back(MakeGround(9, -1));
    es.BuildFromMeshes(meshes);

    RayP ray = BroadcastRay(Point3f(0, 0, 1), Vector3f(0, 0, -1), 100.f);
    // Only lane 0 alive.
    MaskP alive(false);
    alive[0] = true;
    ray.alive = alive;

    HitP hit;
    hit.t = FloatP(-999.f);
    hit.hit = MaskP(false);
    es.Intersect(ray, hit, Coherent::No);

    Check(bool(hit.hit[0]), "lane 0 hit");
    bool dead_ok = true;
    for (size_t i = 1; i < PACKET_WIDTH; ++i) {
        if (hit.hit[i]) dead_ok = false;
        if (!Close(ex(hit.t, i), -999.f)) dead_ok = false;
    }
    Check(dead_ok, "dead lanes untouched");
}

} // namespace

int main() {
    std::cout << "lux2 embree_check (PACKET_WIDTH=" << PACKET_WIDTH << ")"
              << std::endl;

    CheckFrontalHit();
    CheckMissLanes();
    CheckOcclusion();
    CheckMultiMesh();
    CheckShading();
    CheckDegenerate();
    CheckCoherencyParity();
    CheckTailLanes();
    CheckPerfSmoke();

    if (g_failures == 0) {
        std::cout << "ALL EMBREE CHECKS PASSED" << std::endl;
        return EXIT_SUCCESS;
    }
    std::cerr << g_failures << " embree check(s) FAILED" << std::endl;
    return EXIT_FAILURE;
}
