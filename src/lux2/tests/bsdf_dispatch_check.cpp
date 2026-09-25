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

// Verifies enoki::call over a per-lane BSDFPtr.
//
// Two trivial inline stub BSDFs:
//   1. enoki::call partitions a mixed packet by pointer identity and issues
//      exactly ONE vectorized call per distinct material.
//   2. flags() broadcasts to a real UInt32P.
//   3. Pdf leaves inactive lanes at zero.
//   4. SampleF masks every field, so a mixed packet yields per-lane-correct
//      results and inactive lanes are untouched.
//   5. The all-same-pointer fast path equals the mixed path.

#include "core/bsdf.h"
#include "core/bsdf_call.h"
#include "core/bsdfptr_table.h"
#include "core/bsdf_type.h"
#include "core/geometry.h"
#include "core/spectrum.h"
#include "core/vecp.h"

#include <cstdint>
#include <iostream>

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

// Scalar lane access for a FloatP-valued packet component.
template <typename P>
auto lane(const P &v, size_t i) { return v[i]; }

// -----------------------------------------------------------------------
// Two stub BSDFs. Each writes a distinctive, material-keyed value so a
// per-lane result identifies which kernel produced it. Mutable counters
// record how many vectorized calls enoki::call issues per material.
// -----------------------------------------------------------------------

// Diffuse: Pdf = 0.25, SampleF -> wo=+Z, pdf=0.5, f=0.5, DiffuseReflection.
struct TestDiffuse : public BSDF {
    mutable int calls = 0;

    uint32_t flags() const override {
        return uint32_t(BSDFType::DiffuseReflection) |
               uint32_t(BSDFType::FrontSide);
    }

    void SampleF(const SpectrumWavelengthsP &, const Vector3fP &,
                 const DifferentialGeometryP &, const FloatP &, const FloatP &,
                 const FloatP &, BSDFSampleP *s, TransportMode,
                 MaskP active) const override {
        ++calls;
        // Every write MUST be masked or a mixed-packet partition clobbers the
        // lanes owned by the other material (correction A).
        enoki::masked(s->wo, active) = Vector3fP(FloatP(0.f), FloatP(0.f), FloatP(1.f));
        enoki::masked(s->pdf, active) = FloatP(0.5f);
        enoki::masked(s->f, active) = SWCSpectrumP(FloatP(0.5f));
        enoki::masked(s->sampledType, active) =
            UInt32P(uint32_t(BSDFType::DiffuseReflection));
        enoki::masked(s->specular, active) = MaskP(false);
    }

    FloatP Pdf(const SpectrumWavelengthsP &, const Vector3fP &, const Vector3fP &,
               const DifferentialGeometryP &, uint32_t, TransportMode,
               MaskP active) const override {
        ++calls;
        return select(active, FloatP(0.25f), FloatP(0.f));
    }

    void Eval(const SpectrumWavelengthsP &sw, const Vector3fP &wi,
              const Vector3fP &wo, const DifferentialGeometryP &dg,
              TransportMode mode, BSDFEvalP *out, MaskP active) const override {
        ++calls;
        enoki::masked(out->f, active) = SWCSpectrumP(FloatP(0.5f));
        // Forward pdf == Pdf(); reverse pdf == Pdf().
        enoki::masked(out->pdf, active) = Pdf(sw, wi, wo, dg, 0, mode, active);
        enoki::masked(out->pdfRev, active) = Pdf(sw, wo, wi, dg, 0, mode, active);
    }
};

// Specular: Pdf = 0.75, SampleF -> wo=+X, pdf=0.9, f=0.9, SpecularReflection.
struct TestSpecular : public BSDF {
    mutable int calls = 0;

    uint32_t flags() const override {
        return uint32_t(BSDFType::SpecularReflection) |
               uint32_t(BSDFType::FrontSide);
    }

    void SampleF(const SpectrumWavelengthsP &, const Vector3fP &,
                 const DifferentialGeometryP &, const FloatP &, const FloatP &,
                 const FloatP &, BSDFSampleP *s, TransportMode,
                 MaskP active) const override {
        ++calls;
        enoki::masked(s->wo, active) = Vector3fP(FloatP(1.f), FloatP(0.f), FloatP(0.f));
        enoki::masked(s->pdf, active) = FloatP(0.9f);
        enoki::masked(s->f, active) = SWCSpectrumP(FloatP(0.9f));
        enoki::masked(s->sampledType, active) =
            UInt32P(uint32_t(BSDFType::SpecularReflection));
        enoki::masked(s->specular, active) = MaskP(true);
    }

    FloatP Pdf(const SpectrumWavelengthsP &, const Vector3fP &, const Vector3fP &,
               const DifferentialGeometryP &, uint32_t, TransportMode,
               MaskP active) const override {
        ++calls;
        return select(active, FloatP(0.75f), FloatP(0.f));
    }

    // Delta/specular stub: Eval has no finite pdf, writes zeros.
    void Eval(const SpectrumWavelengthsP &, const Vector3fP &, const Vector3fP &,
              const DifferentialGeometryP &, TransportMode, BSDFEvalP *out,
              MaskP active) const override {
        ++calls;
        enoki::masked(out->f, active) = SWCSpectrumP(0.f);
        enoki::masked(out->pdf, active) = FloatP(0.f);
        enoki::masked(out->pdfRev, active) = FloatP(0.f);
    }
};

// A DifferentialGeometryP with all lanes carrying the shading normal +Z.
DifferentialGeometryP MakeDG() {
    DifferentialGeometryP dg;
    dg.p = Point3fP(FloatP(0.f), FloatP(0.f), FloatP(0.f));
    dg.n = Normal3fP(FloatP(0.f), FloatP(0.f), FloatP(1.f));
    dg.uv_u = FloatP(0.f);
    dg.uv_v = FloatP(0.f);
    dg.dp_du = Vector3fP(FloatP(1.f), FloatP(0.f), FloatP(0.f));
    dg.dp_dv = Vector3fP(FloatP(0.f), FloatP(1.f), FloatP(0.f));
    dg.dp_ds = Vector3fP(FloatP(1.f), FloatP(0.f), FloatP(0.f));
    dg.dp_dt = Vector3fP(FloatP(0.f), FloatP(1.f), FloatP(0.f));
    return dg;
}

// A SpectrumWavelengthsP (values irrelevant to the stubs).
SpectrumWavelengthsP MakeSW() {
    SpectrumWavelengthsP sw;
    sw.FromWavelength(FloatP(550.f));
    return sw;
}

// matID: even lanes -> 0 (diffuse), odd lanes -> 1 (specular).
UInt32P MixedMatID() {
    UInt32P id;
    for (size_t i = 0; i < PACKET_WIDTH; ++i)
        id[i] = (i % 2 == 0) ? 0u : 1u;
    return id;
}

// -----------------------------------------------------------------------
// Test 1: flags() broadcasts to a per-lane UInt32P.
// -----------------------------------------------------------------------
void CheckFlagsBroadcast(const BsdfPtrTable &table) {
    BSDFPtr ptr = table.Gather(MixedMatID(), MaskP(true));
    UInt32P f = ptr->flags();

    const uint32_t diff = uint32_t(BSDFType::DiffuseReflection) |
                          uint32_t(BSDFType::FrontSide);
    const uint32_t spec = uint32_t(BSDFType::SpecularReflection) |
                          uint32_t(BSDFType::FrontSide);

    bool ok = true;
    for (size_t i = 0; i < PACKET_WIDTH; ++i)
        ok = ok && (lane(f, i) == ((i % 2 == 0) ? diff : spec));
    Check(ok, "flags() broadcasts per-lane (diffuse/specular)");
}

// -----------------------------------------------------------------------
// Test 2: Pdf partitions; active lanes get their material's value.
// -----------------------------------------------------------------------
void CheckPdfMixed(const BsdfPtrTable &table) {
    TestDiffuse d;
    TestSpecular sp;
    BsdfPtrTable t2;
    t2.ptrs = {&d, &sp};

    BSDFPtr ptr = t2.Gather(MixedMatID(), MaskP(true));
    DifferentialGeometryP dg = MakeDG();
    SpectrumWavelengthsP sw = MakeSW();
    Vector3fP wo(FloatP(0.f), FloatP(0.f), FloatP(-1.f));

    FloatP pdf = ptr->Pdf(sw, wo, wo, dg, uint32_t(BSDFType::All),
                          TransportMode::Radiance, MaskP(true));

    bool ok = true;
    for (size_t i = 0; i < PACKET_WIDTH; ++i)
        ok = ok && (lane(pdf, i) == ((i % 2 == 0) ? 0.25f : 0.75f));
    Check(ok, "Pdf mixed packet: per-lane material value");

    // Partitioning: exactly one vectorized call per distinct material.
    Check(d.calls == 1, "Pdf: diffuse called once (one partition)");
    Check(sp.calls == 1, "Pdf: specular called once (one partition)");
}

// -----------------------------------------------------------------------
// Test 3: Pdf inactive lanes stay zero (value-return masking).
// -----------------------------------------------------------------------
void CheckPdfInactive(const BsdfPtrTable &table) {
    TestDiffuse d;
    TestSpecular sp;
    BsdfPtrTable t2;
    t2.ptrs = {&d, &sp};

    BSDFPtr ptr = t2.Gather(MixedMatID(), MaskP(true));
    DifferentialGeometryP dg = MakeDG();
    SpectrumWavelengthsP sw = MakeSW();
    Vector3fP wo(FloatP(0.f), FloatP(0.f), FloatP(-1.f));

    // Only even lanes active.
    MaskP active;
    for (size_t i = 0; i < PACKET_WIDTH; ++i)
        active[i] = (i % 2 == 0);

    FloatP pdf = ptr->Pdf(sw, wo, wo, dg, uint32_t(BSDFType::All),
                          TransportMode::Radiance, active);

    bool ok = true;
    for (size_t i = 0; i < PACKET_WIDTH; ++i) {
        float expect = (i % 2 == 0) ? 0.25f : 0.f;  // inactive -> zero
        ok = ok && (lane(pdf, i) == expect);
    }
    Check(ok, "Pdf: inactive lanes zero");
}

// -----------------------------------------------------------------------
// Test 4: SampleF masks every field; mixed packet is per-lane correct and
// inactive lanes are untouched (correction A).
// -----------------------------------------------------------------------
void CheckSampleFMixed(const BsdfPtrTable &table) {
    TestDiffuse d;
    TestSpecular sp;
    BsdfPtrTable t2;
    t2.ptrs = {&d, &sp};

    BSDFPtr ptr = t2.Gather(MixedMatID(), MaskP(true));
    DifferentialGeometryP dg = MakeDG();
    SpectrumWavelengthsP sw = MakeSW();
    Vector3fP wo(FloatP(0.f), FloatP(0.f), FloatP(-1.f));

    BSDFSampleP s;
    // Sentinels: if a partition clobbers, or a write is unmasked, these show.
    s.wo = Vector3fP(FloatP(-999.f), FloatP(-999.f), FloatP(-999.f));
    s.pdf = FloatP(-999.f);
    s.f = SWCSpectrumP(FloatP(-999.f));
    s.sampledType = UInt32P(0xFFFFFFFFu);
    s.specular = MaskP(false);

    ptr->SampleF(sw, wo, dg, FloatP(0.1f), FloatP(0.2f), FloatP(0.3f), &s,
                 TransportMode::Radiance, MaskP(true));

    bool pdfOk = true, woOk = true, fOk = true, typeOk = true, specOk = true;
    for (size_t i = 0; i < PACKET_WIDTH; ++i) {
        bool even = (i % 2 == 0);
        pdfOk = pdfOk && (lane(s.pdf, i) == (even ? 0.5f : 0.9f));
        // diffuse wo = +Z, specular wo = +X.
        woOk = woOk && (lane(s.wo.x(), i) == (even ? 0.f : 1.f)) &&
                        (lane(s.wo.z(), i) == (even ? 1.f : 0.f));
        fOk = fOk && (lane(s.f, i) == (even ? 0.5f : 0.9f));
        typeOk = typeOk && (lane(s.sampledType, i) ==
                            (even ? uint32_t(BSDFType::DiffuseReflection)
                                  : uint32_t(BSDFType::SpecularReflection)));
        specOk = specOk && (bool(lane(s.specular, i)) == !even);
    }
    Check(pdfOk, "SampleF mixed: pdf per-lane");
    Check(woOk, "SampleF mixed: wo per-lane (no clobber)");
    Check(fOk, "SampleF mixed: f per-lane (no clobber)");
    Check(typeOk, "SampleF mixed: sampledType per-lane");
    Check(specOk, "SampleF mixed: specular per-lane");
    Check(d.calls == 1 && sp.calls == 1, "SampleF: one call per material");
}

// -----------------------------------------------------------------------
// Test 5: SampleF leaves inactive lanes at their sentinel (masked writes).
// -----------------------------------------------------------------------
void CheckSampleFInactive(const BsdfPtrTable &table) {
    TestDiffuse d;
    TestSpecular sp;
    BsdfPtrTable t2;
    t2.ptrs = {&d, &sp};

    BSDFPtr ptr = t2.Gather(MixedMatID(), MaskP(true));
    DifferentialGeometryP dg = MakeDG();
    SpectrumWavelengthsP sw = MakeSW();
    Vector3fP wo(FloatP(0.f), FloatP(0.f), FloatP(-1.f));

    MaskP active;
    for (size_t i = 0; i < PACKET_WIDTH; ++i)
        active[i] = (i % 2 == 0);  // only even (diffuse) lanes active

    BSDFSampleP s;
    s.wo = Vector3fP(FloatP(-999.f), FloatP(-999.f), FloatP(-999.f));
    s.pdf = FloatP(-999.f);
    s.f = SWCSpectrumP(FloatP(-999.f));
    s.sampledType = UInt32P(0xFFFFFFFFu);
    s.specular = MaskP(false);

    ptr->SampleF(sw, wo, dg, FloatP(0.1f), FloatP(0.2f), FloatP(0.3f), &s,
                 TransportMode::Radiance, active);

    bool activeOk = true, inactiveOk = true;
    for (size_t i = 0; i < PACKET_WIDTH; ++i) {
        if (i % 2 == 0) {
            activeOk = activeOk && (lane(s.pdf, i) == 0.5f);
        } else {
            // Odd lanes inactive: must retain the sentinel.
            inactiveOk = inactiveOk && (lane(s.pdf, i) == -999.f) &&
                         (lane(s.f, i) == -999.f);
        }
    }
    Check(activeOk, "SampleF partial-active: active lanes written");
    Check(inactiveOk, "SampleF partial-active: inactive lanes untouched");
}

// -----------------------------------------------------------------------
// Test 6: all-same-pointer fast path equals the mixed path.
// -----------------------------------------------------------------------
void CheckFastPath(const BsdfPtrTable &table) {
    TestDiffuse d;
    TestSpecular sp;
    BsdfPtrTable t2;
    t2.ptrs = {&d, &sp};

    // All lanes -> diffuse (single partition).
    UInt32P allDiffuse;
    for (size_t i = 0; i < PACKET_WIDTH; ++i)
        allDiffuse[i] = 0u;

    BSDFPtr ptr = t2.Gather(allDiffuse, MaskP(true));
    DifferentialGeometryP dg = MakeDG();
    SpectrumWavelengthsP sw = MakeSW();
    Vector3fP wo(FloatP(0.f), FloatP(0.f), FloatP(-1.f));

    d.calls = sp.calls = 0;
    FloatP pdf = ptr->Pdf(sw, wo, wo, dg, uint32_t(BSDFType::All),
                          TransportMode::Radiance, MaskP(true));

    bool valOk = enoki::all(pdf == FloatP(0.25f));
    Check(valOk, "Fast path: all-diffuse Pdf == 0.25 everywhere");
    Check(d.calls == 1 && sp.calls == 0,
          "Fast path: only diffuse kernel invoked");
}

// -----------------------------------------------------------------------
// Test 7 (Stage 1.5): Eval returns {f, pdf, pdfRev}; for the non-delta
// diffuse stub Eval.pdf must equal Pdf() and pdfRev must equal the reverse
// Pdf; the specular (delta) stub's Eval is all zeros.
// -----------------------------------------------------------------------
void CheckEvalDispatch(const BsdfPtrTable &table) {
    BSDFPtr ptr = table.Gather(MixedMatID(), MaskP(true));
    DifferentialGeometryP dg = MakeDG();
    SpectrumWavelengthsP sw = MakeSW();
    Vector3fP wi(FloatP(0.f), FloatP(0.f), FloatP(1.f));
    Vector3fP wo(FloatP(0.f), FloatP(0.f), FloatP(-1.f));

    BSDFEvalP ev;
    ev.f = SWCSpectrumP(0.f);
    ev.pdf = FloatP(0.f);
    ev.pdfRev = FloatP(0.f);
    ptr->Eval(sw, wi, wo, dg, TransportMode::Radiance, &ev, MaskP(true));
    FloatP pdfDirect = ptr->Pdf(sw, wi, wo, dg, uint32_t(BSDFType::All),
                                TransportMode::Radiance, MaskP(true));

    // Eval.pdf must equal Pdf() on every NON-DELTA lane (NEE uses Eval.pdf,
    // the hit-side MIS term uses Pdf, so they must agree). The specular stub is
    // a delta lobe: its Eval is zero by contract while Pdf() returns a value,
    // so those lanes are excluded from this invariant.
    bool pdfAgrees = true;
    for (size_t i = 0; i < PACKET_WIDTH; ++i)
        if (i % 2 == 0) // diffuse (non-delta) lanes only
            pdfAgrees = pdfAgrees && (lane(ev.pdf, i) == lane(pdfDirect, i));
    Check(pdfAgrees, "Eval.pdf agrees with Pdf() on non-delta lanes");

    // Diffuse (even) lanes: f=0.5, pdf=pdfRev=0.25. Specular (odd): all zero.
    bool fOk = true, revOk = true;
    for (size_t i = 0; i < PACKET_WIDTH; ++i) {
        const bool even = (i % 2 == 0);
        fOk = fOk && (lane(ev.f, i) == (even ? 0.5f : 0.f));
        revOk = revOk && (lane(ev.pdfRev, i) == (even ? 0.25f : 0.f));
    }
    Check(fOk, "Eval.f: diffuse=0.5, specular(delta)=0");
    Check(revOk, "Eval.pdfRev: diffuse=0.25, specular(delta)=0");
}

} // namespace

int main() {
    std::cout << "lux2 bsdf_dispatch_check (PACKET_WIDTH=" << PACKET_WIDTH
              << ")\n";

    // A shared table used only for the flags broadcast (counters unused there).
    TestDiffuse d;
    TestSpecular sp;
    BsdfPtrTable table;
    table.ptrs = {&d, &sp};

    Check(table.ptrs.size() == 2, "table has two materials");

    CheckFlagsBroadcast(table);
    CheckPdfMixed(table);
    CheckPdfInactive(table);
    CheckSampleFMixed(table);
    CheckSampleFInactive(table);
    CheckFastPath(table);
    CheckEvalDispatch(table);

    if (g_failures == 0) {
        std::cout << "lux2bsdfdispatchcheck: ALL CHECKS PASSED\n";
        return 0;
    }
    std::cerr << "lux2bsdfdispatchcheck: " << g_failures << " FAILURE(S)\n";
    return 1;
}
