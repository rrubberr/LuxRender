/***************************************************************************
 *   This file is part of LuxRender.                                       *
 *                                                                         *
 *   To the extent possible under law, the author(s) have dedicated all    *
 *   copyright and related neighboring rights to the below code to the     *
 *   public domain worldwide. The below code is distributed without any    *
 *   warranty.                                                             *
 *                                                                         *
 *   See: <https://creativecommons.org/publicdomain/zero/1.0/>             *
 *                                                                         *
 ***************************************************************************/
// Tests are machine generated. Proceed with caution!

// EmbreeScene build + packet intersect/occlude/shade.

#include "core/accel/embree2.h"
#include "core/scene.h"
#include "core/shape.h"
#include "core/math/ray.h"

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

// Axis-aligned cube centered at origin, half-size h, with per-triangle
// medium binding (front = outward/exterior side, back = interior). Winding
// is generated so Embree's geometric normal points outward on every face.
MeshDesc MakeMediumCube(float h, std::uint32_t frontID, std::uint32_t backID) {
    MeshDesc m;
    const Normal3f N[6] = {{1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
                           {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};
    // Tangents U,V chosen so cross(U,V) == N (right-handed outward winding).
    const Vector3f U[6] = {{0, 1, 0},  {0, -1, 0}, {0, 0, 1},
                           {0, 0, -1}, {1, 0, 0},  {-1, 0, 0}};
    const Vector3f V[6] = {{0, 0, 1},  {0, 0, 1},  {1, 0, 0},
                           {1, 0, 0},  {0, 1, 0},  {0, 1, 0}};
    for (int f = 0; f < 6; ++f) {
        const Point3f c(N[f].x() * h, N[f].y() * h, N[f].z() * h);
        auto corner = [&](float su, float sv) {
            return Point3f(c.x() + su * U[f].x() * h + sv * V[f].x() * h,
                           c.y() + su * U[f].y() * h + sv * V[f].y() * h,
                           c.z() + su * U[f].z() * h + sv * V[f].z() * h);
        };
        const Point3f p0 = corner(-1, -1), p1 = corner(1, -1),
                      p2 = corner(1, 1), p3 = corner(-1, 1);
        Normal3f nn(N[f].x(), N[f].y(), N[f].z());
        TriangleDesc a = MakeTri(p0, p1, p2, nn, nn, nn, UV(0, 0), UV(1, 0),
                                 UV(1, 1), 1, -1);
        TriangleDesc b = MakeTri(p0, p2, p3, nn, nn, nn, UV(0, 0), UV(1, 1),
                                 UV(0, 1), 1, -1);
        a.mediumFrontID = frontID; a.mediumBackID = backID;
        b.mediumFrontID = frontID; b.mediumBackID = backID;
        m.tris.push_back(a);
        m.tris.push_back(b);
    }
    m.bound = BBox(Point3f(-h, -h, -h), Point3f(h, h, h));
    return m;
}

// MediumIDAt: point inside a closed volume resolves to the interior medium.
void CheckMediumInside() {
    std::cout << "[medium inside]" << std::endl;
    EmbreeScene es;
    std::vector<MeshDesc> meshes;
    meshes.push_back(MakeMediumCube(1.f, 0u, 2u)); // exterior clear, interior 2
    es.BuildFromMeshes(meshes);
    Check(es.MediumIDAt(Point3f(0.f, 0.f, 0.f)) == 2u, "center -> interior 2");
    Check(es.MediumIDAt(Point3f(0.5f, -0.3f, 0.7f)) == 2u,
          "off-center inside -> 2");
}

// MediumIDAt: point outside a closed volume resolves to the exterior (clear).
void CheckMediumOutside() {
    std::cout << "[medium outside]" << std::endl;
    EmbreeScene es;
    std::vector<MeshDesc> meshes;
    meshes.push_back(MakeMediumCube(1.f, 0u, 2u));
    es.BuildFromMeshes(meshes);
    Check(es.MediumIDAt(Point3f(3.f, 0.f, 0.f)) == 0u, "outside +X -> clear");
    Check(es.MediumIDAt(Point3f(-2.f, 5.f, -1.f)) == 0u,
          "far outside -> clear");
}

// MediumIDAt: a non-boundary triangle (front == back) is skipped, so a point
// beside an open quad resolves to clear rather than the quad's medium.
void CheckMediumSkipsNonBoundary() {
    std::cout << "[medium skips non-boundary]" << std::endl;
    EmbreeScene es;
    std::vector<MeshDesc> meshes;
    MeshDesc quad = MakeGround(9, -1); // z=0 plane, front==back==0
    for (auto &t : quad.tris) {
        t.mediumFrontID = 5u;
        t.mediumBackID = 5u; // non-boundary: both sides identical
    }
    meshes.push_back(quad);
    es.BuildFromMeshes(meshes);
    // Point on the plane and a point above it: the quad is not an interface.
    Check(es.MediumIDAt(Point3f(0.f, 0.f, 0.f)) == 0u, "on quad -> clear");
    Check(es.MediumIDAt(Point3f(0.f, 0.f, 0.5f)) == 0u, "above quad -> clear");
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

// Transposed ShadeHit must match the gather reference on identical inputs.
// A grid of quads with per-triangle attributes and per-lane ray origins makes
// lanes land on different triangles, exercising the record addressing.
void CheckShadeEquivalence() {
    std::cout << "[shade transposed vs gather]" << std::endl;
    EmbreeScene es;
    std::vector<MeshDesc> meshes;
    // 4x4 grid of quads (2 tris each) in z=0, cell [-1,1] -> covers
    // [-4,4]. Distinct matID/lightID/UVs per triangle.
    for (int gy = 0; gy < 4; ++gy) {
        for (int gx = 0; gx < 4; ++gx) {
            float x0 = gx * 2.f - 4.f, y0 = gy * 2.f - 4.f;
            Normal3f up(0, 0, 1);
            std::uint32_t base = (std::uint32_t)(gy * 4 + gx) * 2u;
            MeshDesc m;
            m.tris.push_back(MakeTri(
                Point3f(x0, y0, 0), Point3f(x0 + 2, y0, 0),
                Point3f(x0 + 2, y0 + 2, 0), up, up, up,
                UV(0, 0), UV(1, 0), UV(1, 1),
                base + 1, (std::int32_t)(base % 5) - 1));
            m.tris.push_back(MakeTri(
                Point3f(x0, y0, 0), Point3f(x0 + 2, y0 + 2, 0),
                Point3f(x0, y0 + 2, 0), up, up, up,
                UV(0, 0), UV(1, 1), UV(0, 1),
                base + 101, (std::int32_t)(base % 7) - 1));
            m.bound = BBox(Point3f(x0, y0, 0), Point3f(x0 + 2, y0 + 2, 0));
            meshes.push_back(m);
        }
    }
    es.BuildFromMeshes(meshes);

    // Lane i aims straight down at a distinct point in the grid.
    float ox[PACKET_WIDTH], oy[PACKET_WIDTH];
    for (size_t i = 0; i < PACKET_WIDTH; ++i) {
        ox[i] = -3.5f + 7.f * (float(i) + 0.37f) / float(PACKET_WIDTH);
        oy[i] = -3.5f + 7.f * (float(i) + 0.61f) / float(PACKET_WIDTH);
    }
    RayP ray;
    ray.o = Point3fP(enoki::load_unaligned<FloatP>(ox),
                     enoki::load_unaligned<FloatP>(oy), FloatP(1.f));
    ray.d = Vector3fP(FloatP(0.f), FloatP(0.f), FloatP(-1.f));
    ray.mint = FloatP(0.f);
    ray.maxt = FloatP(100.f);
    ray.time = FloatP(0.f);
    ray.InitPayload();

    HitP hit;
    es.Intersect(ray, hit, Coherent::No);
    bool anyHit = enoki::any(hit.hit);
    Check(anyHit, "grid rays hit");

    HitP g = hit; // copy: ShadeHit mutates only the derived fields
    es.ShadeHit(hit);
    es.ShadeHit_gather(g);

    bool eq = true;
    auto cmpF = [&](const FloatP &a, const FloatP &b, const char *nm) {
        for (size_t i = 0; i < PACKET_WIDTH; ++i) {
            if (!hit.hit[i]) continue;
            if (!Close(float(a[i]), float(b[i]), 1e-5f)) {
                std::cerr << "    shade diff " << nm << " lane " << i
                          << ": " << float(a[i]) << " vs " << float(b[i])
                          << std::endl;
                eq = false;
            }
        }
    };
    cmpF(hit.p.x(), g.p.x(), "p.x");
    cmpF(hit.p.y(), g.p.y(), "p.y");
    cmpF(hit.p.z(), g.p.z(), "p.z");
    cmpF(hit.sh_n.x(), g.sh_n.x(), "sh_n.x");
    cmpF(hit.sh_n.y(), g.sh_n.y(), "sh_n.y");
    cmpF(hit.sh_n.z(), g.sh_n.z(), "sh_n.z");
    cmpF(hit.uv.x(), g.uv.x(), "uv.x");
    cmpF(hit.uv.y(), g.uv.y(), "uv.y");
    cmpF(hit.dp_du.x(), g.dp_du.x(), "dp_du.x");
    cmpF(hit.dp_du.y(), g.dp_du.y(), "dp_du.y");
    cmpF(hit.dp_du.z(), g.dp_du.z(), "dp_du.z");
    cmpF(hit.dp_dv.x(), g.dp_dv.x(), "dp_dv.x");
    cmpF(hit.dp_dv.z(), g.dp_dv.z(), "dp_dv.z");
    cmpF(hit.dp_ds.x(), g.dp_ds.x(), "dp_ds.x");
    cmpF(hit.dp_dt.y(), g.dp_dt.y(), "dp_dt.y");
    Check(eq, "transposed ShadeHit matches gather reference");

    bool idsEq = true;
    for (size_t i = 0; i < PACKET_WIDTH; ++i) {
        if (!hit.hit[i]) continue;
        if (hit.matID[i] != g.matID[i] || hit.lightID[i] != g.lightID[i] ||
            hit.mediumFrontID[i] != g.mediumFrontID[i] ||
            hit.mediumBackID[i] != g.mediumBackID[i]) {
            std::cerr << "    id diff lane " << i << ": mat "
                      << uint32_t(hit.matID[i]) << " vs " << uint32_t(g.matID[i])
                      << std::endl;
            idsEq = false;
        }
    }
    Check(idsEq, "record IDs match gather reference");

    // Partial active mask: force some lanes inactive; both paths must agree
    // (inactive lanes zero-filled, records untouched).
    HitP h2 = hit, g2 = hit;
    MaskP part(false);
    for (size_t i = 0; i < PACKET_WIDTH; ++i)
        part[i] = (i % 3u) != 1u;
    h2.hit = part && hit.hit;
    g2.hit = part && hit.hit;
    es.ShadeHit(h2);
    es.ShadeHit_gather(g2);
    bool peq = true;
    const FloatP hpx = h2.p.x(), gpx = g2.p.x();
    const FloatP hsz = h2.sh_n.z(), gsz = g2.sh_n.z();
    for (size_t i = 0; i < PACKET_WIDTH; ++i) {
        if (!h2.hit[i]) {
            // Inactive: transposed path must leave fields zeroed like gather.
            if (float(hpx[i]) != float(gpx[i])) peq = false;
            continue;
        }
        if (!Close(float(hpx[i]), float(gpx[i]), 1e-5f) ||
            !Close(float(hsz[i]), float(gsz[i]), 1e-5f) ||
            h2.matID[i] != g2.matID[i])
            peq = false;
    }
    Check(peq, "partial-mask ShadeHit matches gather reference");
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
    CheckShadeEquivalence();
    CheckDegenerate();
    CheckCoherencyParity();
    CheckTailLanes();
    CheckMediumInside();
    CheckMediumOutside();
    CheckMediumSkipsNonBoundary();
    CheckPerfSmoke();

    if (g_failures == 0) {
        std::cout << "ALL EMBREE CHECKS PASSED" << std::endl;
        return EXIT_SUCCESS;
    }
    std::cerr << g_failures << " embree check(s) FAILED" << std::endl;
    return EXIT_FAILURE;
}
