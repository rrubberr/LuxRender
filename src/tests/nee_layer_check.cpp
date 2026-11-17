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

// Guards the two Phase 2 divergences that today's materials cannot exercise.
//
//   1. TRIPWIRE. Every registered material is built with default parameters
//      and its flags() inspected. If any reports a non-delta transmission lobe
//      (DiffuseTransmission | GlossyTransmission), the check fails with
//      "add the transmission oracle fixture": at that point ShadowOrigin's
//      light-side offset and RecvHasNormal become reachable, and agreeing
//      renders of the existing fixtures no longer prove anything. The oracle is
//      a rough-dielectric or translucent slab rendered lightmode=nee|bsdf|mis.
//
//   2. ShadowOrigin offsets toward the side wi leaves through, and leaves a
//      medium vertex (ng == 0) where it is.
//
//   3. RecvHasNormal is true for a surface with a reflection-only lobe, false
//      for a medium vertex and for a surface with a transmission lobe.

#include "core/bsdf.h"
#include "core/bsdf_call.h"
#include "core/bsdf_type.h"
#include "core/bsdfptr_table.h"
#include "core/color/spectrum.h"
#include "core/dynload.h"
#include "core/math/math.h"
#include "core/math/vecp.h"
#include "integrators/core/vertex.h"
#include "integrators/lane/direct.h"

#include <cstdint>
#include <iostream>
#include <string>

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

template <typename P>
auto lane(const P &v, size_t i) { return v[i]; }

constexpr uint32_t kTransmit =
    uint32_t(BSDFType::DiffuseTransmission) |
    uint32_t(BSDFType::GlossyTransmission);

// -----------------------------------------------------------------------
// Stub BSDFs: one reflection-only, one with a diffuse transmission lobe.
// Only flags() matters; the rest of the interface is inert.
// -----------------------------------------------------------------------

struct ReflOnly : public BSDF {
    uint32_t flags() const override {
        return uint32_t(BSDFType::DiffuseReflection) |
               uint32_t(BSDFType::FrontSide) | uint32_t(BSDFType::BackSide);
    }
    void SampleF(const SpectrumWavelengthsP &, const Vector3fP &,
                 const DifferentialGeometryP &, const FloatP &, const FloatP &,
                 const FloatP &, BSDFSampleP *, TransportMode,
                 MaskP) const override {}
    FloatP Pdf(const SpectrumWavelengthsP &, const Vector3fP &, const Vector3fP &,
               const DifferentialGeometryP &, uint32_t, TransportMode,
               MaskP a) const override {
        return select(a, FloatP(1.f), FloatP(0.f));
    }
    void Eval(const SpectrumWavelengthsP &, const Vector3fP &, const Vector3fP &,
              const DifferentialGeometryP &, TransportMode, BSDFEvalP *o,
              MaskP a) const override {
        enoki::masked(o->f, a) = SWCSpectrumP(0.f);
        enoki::masked(o->pdf, a) = FloatP(0.f);
        enoki::masked(o->pdfRev, a) = FloatP(0.f);
    }
};

struct Transmissive : public BSDF {
    uint32_t flags() const override {
        return uint32_t(BSDFType::DiffuseReflection) |
               uint32_t(BSDFType::DiffuseTransmission) |
               uint32_t(BSDFType::FrontSide) | uint32_t(BSDFType::BackSide);
    }
    void SampleF(const SpectrumWavelengthsP &, const Vector3fP &,
                 const DifferentialGeometryP &, const FloatP &, const FloatP &,
                 const FloatP &, BSDFSampleP *, TransportMode,
                 MaskP) const override {}
    FloatP Pdf(const SpectrumWavelengthsP &, const Vector3fP &, const Vector3fP &,
               const DifferentialGeometryP &, uint32_t, TransportMode,
               MaskP a) const override {
        return select(a, FloatP(1.f), FloatP(0.f));
    }
    void Eval(const SpectrumWavelengthsP &, const Vector3fP &, const Vector3fP &,
              const DifferentialGeometryP &, TransportMode, BSDFEvalP *o,
              MaskP a) const override {
        enoki::masked(o->f, a) = SWCSpectrumP(0.f);
        enoki::masked(o->pdf, a) = FloatP(0.f);
        enoki::masked(o->pdfRev, a) = FloatP(0.f);
    }
};

// -----------------------------------------------------------------------
// 1. Tripwire over the registered materials.
// -----------------------------------------------------------------------
void CheckTripwire() {
    auto &reg = DynamicLoader::registeredMaterials();
    Check(!reg.empty(), "material registry is populated");

    bool offender = false;
    for (auto &kv : reg) {
        const std::string &name = kv.first;
        // Default parameters: every material guards a null ParamSet.
        PluginContext ctx;
        std::shared_ptr<Material> m;
        try {
            m = kv.second(ctx);
        } catch (...) {
            m.reset();
        }
        if (!m) {
            std::cerr << "  note: " << name
                      << " could not be built with default parameters"
                      << std::endl;
            continue;
        }
        const uint32_t f = m->flags();
        if ((f & kTransmit) != 0u) {
            std::cerr << "  FAIL: " << name << " reports a non-delta"
                      << " transmission lobe (flags 0x" << std::hex << f
                      << std::dec << ") -- add the transmission oracle fixture"
                      << std::endl;
            offender = true;
        } else {
            std::cout << "  ok:   " << name
                      << " has no non-delta transmission lobe" << std::endl;
        }
    }
    if (offender)
        ++g_failures;
}

// -----------------------------------------------------------------------
// 2. ShadowOrigin.
// -----------------------------------------------------------------------
void CheckShadowOrigin() {
    const FloatP eps(EPS_RAY);

    VertexP v;
    v.valid = MaskP(true);
    v.isMedium = MaskP(false);
    v.p = Point3fP(FloatP(1.f), FloatP(2.f), FloatP(3.f));
    v.dg.ng = Normal3fP(FloatP(0.f), FloatP(0.f), FloatP(1.f));

    // Leaving through the front face: +ng.
    {
        const Vector3fP wi(FloatP(0.f), FloatP(0.f), FloatP(1.f));
        const Point3fP o = ShadowOrigin(v, wi);
        Check(lane(o.z(), 0) > 3.f, "ShadowOrigin: front-side leave pushes along +ng");
        Check(lane(o.x(), 0) == 1.f && lane(o.y(), 0) == 2.f,
              "ShadowOrigin: tangential position unchanged");
    }
    // Leaving through the back face: -ng.
    {
        const Vector3fP wi(FloatP(0.f), FloatP(0.f), FloatP(-1.f));
        const Point3fP o = ShadowOrigin(v, wi);
        Check(lane(o.z(), 0) < 3.f, "ShadowOrigin: back-side leave pushes along -ng");
    }
    // A medium vertex carries ng == 0: dot is never > 0, so the select picks
    // -ng, which is still zero. The scatter point must not move.
    {
        VertexP mv = v;
        mv.isMedium = MaskP(true);
        mv.dg.ng = Normal3fP(FloatP(0.f), FloatP(0.f), FloatP(0.f));
        const Vector3fP wi(FloatP(0.4f), FloatP(-0.3f), FloatP(0.5f));
        const Point3fP o = ShadowOrigin(mv, wi);
        Check(lane(o.x(), 0) == 1.f && lane(o.y(), 0) == 2.f &&
                  lane(o.z(), 0) == 3.f,
              "ShadowOrigin: zero ng leaves the scatter point put");
    }
    // Offset magnitude is EPS_RAY. Measured from the origin so the subtraction
    // that recovers it is exact in float32.
    {
        VertexP ov = v;
        ov.p = Point3fP(FloatP(0.f), FloatP(0.f), FloatP(0.f));
        const Vector3fP wi(FloatP(0.f), FloatP(0.f), FloatP(1.f));
        const Point3fP o = ShadowOrigin(ov, wi);
        Check(lane(o.z(), 0) == lane(eps, 0),
              "ShadowOrigin: offset is exactly EPS_RAY");
    }
}

// -----------------------------------------------------------------------
// 3. RecvHasNormal.
// -----------------------------------------------------------------------
void CheckRecvHasNormal() {
    ReflOnly refl;
    Transmissive trans;
    BsdfPtrTable table;
    table.ptrs = {&refl, &trans};

    // Lane 0: reflection-only surface. Lane 1: transmitting surface.
    UInt32P id;
    for (size_t i = 0; i < PACKET_WIDTH; ++i)
        id[i] = (i % 2 == 0) ? 0u : 1u;
    const MaskP surf(true);
    BSDFPtr bsdf = table.Gather(id, surf);

    const MaskP r = RecvHasNormal(surf, bsdf);
    bool ok = true;
    for (size_t i = 0; i < PACKET_WIDTH; ++i)
        ok = ok && (lane(r, i) == (i % 2 == 0));
    Check(ok, "RecvHasNormal: reflection-only true, transmitting false");

    // A medium vertex is never tested, whatever the pointer says.
    const MaskP med(false);
    const MaskP rm = RecvHasNormal(med, bsdf);
    Check(!enoki::any(rm), "RecvHasNormal: medium lanes excluded");

    // The mask also keeps a null pointer out of the dispatch: only lane 0's
    // pointer is valid here, and the result must be false everywhere.
    UInt32P half;
    for (size_t i = 0; i < PACKET_WIDTH; ++i)
        half[i] = (i == 0) ? 0u : 0xFFFFFFFFu;
    const MaskP only0 = eq(half, UInt32P(0u));
    BSDFPtr sparse = table.Gather(half, only0);
    const MaskP rs = RecvHasNormal(only0, sparse);
    bool ok2 = lane(rs, 0);
    for (size_t i = 1; i < PACKET_WIDTH; ++i)
        ok2 = ok2 && !lane(rs, i);
    Check(ok2, "RecvHasNormal: inactive lanes false with a null pointer");
}

} // namespace

int main() {
    std::cout << "lux2 NEE layer check (transmission tripwire, ShadowOrigin, "
                 "RecvHasNormal)\n";
    CheckTripwire();
    CheckShadowOrigin();
    CheckRecvHasNormal();

    if (g_failures == 0) {
        std::cout << "ALL NEE LAYER CHECKS PASSED\n";
        return 0;
    }
    std::cout << g_failures << " NEE LAYER CHECK(S) FAILED\n";
    return 1;
}
