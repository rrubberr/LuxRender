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

// Advance(): intersect + Null collapse classification.
//
// Media (scatter / absorb) are exercised by the integrator renders and the
// medium check; here they are covered only at compile time, since a scatter
// stub would have to reimplement real flight sampling. What this test pins is
// the part unique to Advance: a Null surface is not a vertex -- the ray passes
// through it (OnPassThrough fires, no bounce) and lands on the first real
// surface behind it.

#include "integrators/lane/advance.h"

#include "core/accel/embree2.h"
#include "core/bsdf.h"
#include "core/bsdfptr_table.h"
#include "core/mediumptr_table.h"
#include "core/scene.h"
#include "core/shape.h"

#include <cstdint>
#include <iostream>
#include <vector>

using namespace lux2;

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

// A BSDF whose only behaviour is its flag set; the walk never samples it.
class StubBSDF : public BSDF
{
  public:
    explicit StubBSDF(uint32_t f) : m_flags(f) {}
    uint32_t flags() const override { return m_flags; }
    void SampleF(const SpectrumWavelengthsP &, const Vector3fP &,
                 const DifferentialGeometryP &, const FloatP &, const FloatP &,
                 const FloatP &, BSDFSampleP *, TransportMode,
                 MaskP) const override {}
    FloatP Pdf(const SpectrumWavelengthsP &, const Vector3fP &,
               const Vector3fP &, const DifferentialGeometryP &, uint32_t,
               TransportMode, MaskP) const override
    {
        return FloatP(0.f);
    }
    void Eval(const SpectrumWavelengthsP &, const Vector3fP &,
              const Vector3fP &, const DifferentialGeometryP &, TransportMode,
              BSDFEvalP *, MaskP) const override {}

  private:
    uint32_t m_flags;
};

// Records how many times a Null crossing was reported per lane.
struct CountConsumer
{
    Int32P passThroughs;
    CountConsumer() : passThroughs(Int32P(0)) {}
    Int32P Bounce(const LaneCore &) const { return Int32P(0); }
    Int32P DepthBase(const LaneCore &) const { return Int32P(0); }
    void OnPassThrough(const LaneCore &, const HitP &h, const MaskP &m)
    {
        passThroughs = passThroughs + select(m, Int32P(1), Int32P(0));
        (void)h;
    }
};

TriangleDesc Tri(Point3f v0, Point3f v1, Point3f v2, Normal3f n, uint32_t mat)
{
    TriangleDesc t;
    t.v0 = v0; t.v1 = v1; t.v2 = v2;
    t.n0 = n; t.n1 = n; t.n2 = n;
    t.uv0 = UV(0, 0); t.uv1 = UV(1, 0); t.uv2 = UV(1, 1);
    t.matID = mat; t.lightID = -1; t.groupMask = 0xFFFFFFFFu;
    return t;
}

// Two triangles forming a quad at height z, facing +z.
MeshDesc QuadAt(float z, uint32_t mat)
{
    MeshDesc m;
    Normal3f up(0.f, 0.f, 1.f);
    m.tris.push_back(Tri(Point3f(-2, -2, z), Point3f(2, -2, z),
                         Point3f(2, 2, z), up, mat));
    m.tris.push_back(Tri(Point3f(-2, -2, z), Point3f(2, 2, z),
                         Point3f(-2, 2, z), up, mat));
    m.bound = BBox(Point3f(-2, -2, z), Point3f(2, 2, z));
    return m;
}

LaneCore MakeLane(Point3f o, Vector3f d)
{
    LaneCore c;
    c.o = Point3fP(FloatP(o.x()), FloatP(o.y()), FloatP(o.z()));
    c.d = Vector3fP(FloatP(d.x()), FloatP(d.y()), FloatP(d.z()));
    c.mint = FloatP(0.f);
    c.maxt = FloatP(1000.f);
    c.time = FloatP(0.f);
    c.medium = UInt32P(LUX2_MEDIUM_CLEAR);
    c.sw.FromWavelength(FloatP(550.f));
    c.beta = SWCSpectrumP(1.f);
    c.seed = UInt32P(1u);
    c.sampleIdx = UInt32P(0u);
    c.alive = MaskP(true);
    return c;
}

// mat 0 = diffuse, mat 1 = Null.
void Setup(TileCtx &ctx, EmbreeScene &es, BsdfPtrTable &table,
           MediumPtrTable &mtab, const std::vector<MeshDesc> &meshes)
{
    static StubBSDF diffuse(uint32_t(BSDFType::DiffuseReflection) |
                            uint32_t(BSDFType::FrontSide));
    static StubBSDF nullBsdf(uint32_t(BSDFType::Null) |
                             uint32_t(BSDFType::FrontSide) |
                             uint32_t(BSDFType::BackSide));
    table.ptrs = {&diffuse, &nullBsdf};
    mtab.ptrs = {};
    mtab.RebuildNonVacuum();
    es.BuildFromMeshes(meshes);
    ctx.embree = &es;
    ctx.table = &table;
    ctx.mediumTable = &mtab;
    ctx.hasMedia = false;
}

// A ray through a Null quad onto a diffuse quad behind it: one pass-through,
// surface on the diffuse plane, no escape/scatter/absorb.
void CheckNullCollapse()
{
    std::cout << "[null collapse]" << std::endl;
    EmbreeScene es;
    BsdfPtrTable table;
    MediumPtrTable mtab;
    TileCtx ctx;
    Setup(ctx, es, table, mtab, {QuadAt(0.f, 1), QuadAt(-1.f, 0)});

    CountConsumer cons;
    LaneCore lane = MakeLane(Point3f(0, 0, 1), Vector3f(0, 0, -1));
    Event ev = Advance<Dir::Eye>(ctx, lane, cons, lane.alive);

    Check(all(eq(ev.surface, MaskP(true))), "surface hit after collapse");
    Check(none(ev.escape) && none(ev.scatter) && none(ev.absorbed),
          "no escape/scatter/absorb");
    Check(cons.passThroughs[0] == 1, "one OnPassThrough for one Null");
    Check(std::fabs(ev.hit.p.z()[0] - (-1.f)) < 1e-3f,
          "landed on the diffuse plane behind the Null");
}

// Two stacked Null quads then a diffuse plane: two pass-throughs.
void CheckDoubleNull()
{
    std::cout << "[double null]" << std::endl;
    EmbreeScene es;
    BsdfPtrTable table;
    MediumPtrTable mtab;
    TileCtx ctx;
    Setup(ctx, es, table, mtab,
          {QuadAt(0.f, 1), QuadAt(-0.5f, 1), QuadAt(-1.f, 0)});

    CountConsumer cons;
    LaneCore lane = MakeLane(Point3f(0, 0, 1), Vector3f(0, 0, -1));
    Event ev = Advance<Dir::Eye>(ctx, lane, cons, lane.alive);

    Check(all(eq(ev.surface, MaskP(true))), "surface hit after two collapses");
    Check(cons.passThroughs[0] == 2, "two OnPassThrough for two Nulls");
}

// A ray that hits nothing: escape.
void CheckEscape()
{
    std::cout << "[escape]" << std::endl;
    EmbreeScene es;
    BsdfPtrTable table;
    MediumPtrTable mtab;
    TileCtx ctx;
    Setup(ctx, es, table, mtab, {QuadAt(-1.f, 0)});

    CountConsumer cons;
    // Aim away from the quad (upward).
    LaneCore lane = MakeLane(Point3f(0, 0, 1), Vector3f(0, 0, 1));
    Event ev = Advance<Dir::Eye>(ctx, lane, cons, lane.alive);

    Check(all(eq(ev.escape, MaskP(true))), "escaped to the background");
    Check(none(ev.surface) && none(ev.scatter) && none(ev.absorbed),
          "no surface/scatter/absorb on escape");
}

// A direct hit on a diffuse plane: surface, zero pass-throughs.
void CheckOpaque()
{
    std::cout << "[opaque]" << std::endl;
    EmbreeScene es;
    BsdfPtrTable table;
    MediumPtrTable mtab;
    TileCtx ctx;
    Setup(ctx, es, table, mtab, {QuadAt(-1.f, 0)});

    CountConsumer cons;
    LaneCore lane = MakeLane(Point3f(0, 0, 1), Vector3f(0, 0, -1));
    Event ev = Advance<Dir::Eye>(ctx, lane, cons, lane.alive);

    Check(all(eq(ev.surface, MaskP(true))), "surface on opaque hit");
    Check(cons.passThroughs[0] == 0, "no pass-through on opaque");
}

} // namespace

int main()
{
    CheckNullCollapse();
    CheckDoubleNull();
    CheckEscape();
    CheckOpaque();
    if (g_failures)
    {
        std::cerr << g_failures << " advance check(s) failed" << std::endl;
        return 1;
    }
    std::cout << "advance checks passed" << std::endl;
    return 0;
}
