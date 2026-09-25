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

// Verifies the D.1 matte material (Lambertian + Oren--Nayar).
//
//   1. flags() == DiffuseReflection | FrontSide.
//   2. SampleF yields a hemisphere direction, pdf >= 0, finite f,
//      sampledType == DiffuseReflection, specular == false.
//   3. Pdf matches cos(theta) * INVPI on the front side, 0 on the back.
//   4. At sigma == 0 the lobe equals kd/pi (Lambertian, cos pulled out).
//   5. At sigma > 0 the lobe deviates from kd/pi (Oren--Nayar active).

#include "core/bsdf.h"
#include "core/bsdf_type.h"
#include "core/spectrum.h"
#include "core/texture.h"
#include "core/vecp.h"
#include "core/math.h"
#include "core/fresnel.h"
#include "materials/matte.h"
#include "materials/metal2.h"
#include "materials/glass.h"
#include "textures/constant.h"
#include "textures/fresnelcolor.h"

#include <cmath>
#include <iostream>
#include <memory>

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

SpectrumWavelengthsP MakeSW() {
    SpectrumWavelengthsP sw;
    sw.Sample(FloatP(0.5f));
    return sw;
}

std::shared_ptr<MatteMaterial> MakeMatte(float sigmaDeg) {
    auto kd = std::make_shared<ConstantColorTexture>(
        RGBColorP(FloatP(0.8f), FloatP(0.8f), FloatP(0.8f)));
    auto sigma = std::make_shared<ConstantFloatTexture>(FloatP(sigmaDeg));
    return std::make_shared<MatteMaterial>(kd, sigma);
}

// wo pointing into the surface from above (front side: dot(wo, n) < 0).
Vector3fP MakeWo() {
    Vector3fP wo(FloatP(0.3f), FloatP(-0.2f), FloatP(-1.f));
    return enoki::normalize(wo);
}

// wo on the +n hemisphere (lux2 convention: wo = -incidentDir, face-forward).
// Required by the reflection lobe (lux rejects wo.z <= 0).
Vector3fP MakeWoRefl() {
    Vector3fP wo(FloatP(0.3f), FloatP(-0.2f), FloatP(1.f));
    return enoki::normalize(wo);
}

void CheckFlags() {
    auto m = MakeMatte(0.f);
    const uint32_t expected = uint32_t(BSDFType::DiffuseReflection) |
                              uint32_t(BSDFType::FrontSide);
    Check(m->flags() == expected, "matte flags == DiffuseReflection | FrontSide");
}

void CheckSample() {
    auto m = MakeMatte(0.f);
    const DifferentialGeometryP dg = MakeDG();
    const SpectrumWavelengthsP sw = MakeSW();
    const Vector3fP wo = MakeWo();

    BSDFSampleP s;
    s.pdf = FloatP(-999.f);
    m->SampleF(sw, wo, dg, FloatP(0.37f), FloatP(0.61f), FloatP(0.5f),
               &s, TransportMode::Radiance, MaskP(true));

    bool pdfOk = true, fFinite = true, hemiOk = true, typeOk = true, specOk = true;
    for (size_t i = 0; i < PACKET_WIDTH; ++i) {
        if (!(lane(s.pdf, i) >= 0.f)) pdfOk = false;
        float x = lane(s.f, i);
        if (!(x >= 0.f) || !(x < 1e30f)) fFinite = false;
        float dn = lane(s.wo, 2)[i];  // z component (n == +Z)
        if (!(dn > 0.f)) hemiOk = false;
        if (lane(s.sampledType, i) != uint32_t(BSDFType::DiffuseReflection))
            typeOk = false;
        if (lane(s.specular, i)) specOk = false;
    }
    Check(pdfOk, "SampleF pdf >= 0 on every lane");
    Check(fFinite, "SampleF f finite and non-negative");
    Check(hemiOk, "SampleF wi on the +n hemisphere");
    Check(typeOk, "SampleF sampledType == DiffuseReflection");
    Check(specOk, "SampleF specular == false");
}

void CheckPdf() {
    auto m = MakeMatte(0.f);
    const DifferentialGeometryP dg = MakeDG();
    const SpectrumWavelengthsP sw = MakeSW();
    const Vector3fP wo = MakeWo();

    // Front-side direction: expect cos(theta) * INVPI.
    Vector3fP wiF = enoki::normalize(Vector3fP(FloatP(0.2f), FloatP(0.1f), FloatP(0.9f)));
    FloatP pdfF = m->Pdf(sw, wiF, wo, dg, uint32_t(BSDFType::All),
                         TransportMode::Radiance, MaskP(true));
    float cosT = 0.9f / std::sqrt(0.2f * 0.2f + 0.1f * 0.1f + 0.9f * 0.9f);
    float expect = cosT * INVPI;
    bool frontOk = true;
    for (size_t i = 0; i < PACKET_WIDTH; ++i)
        if (std::fabs(lane(pdfF, i) - expect) > 1e-4f) frontOk = false;
    Check(frontOk, "Pdf == cos(theta)*INVPI on the front side");

    // Back-side direction: expect 0.
    Vector3fP wiB = enoki::normalize(Vector3fP(FloatP(0.2f), FloatP(0.1f), FloatP(-0.9f)));
    FloatP pdfB = m->Pdf(sw, wiB, wo, dg, uint32_t(BSDFType::All),
                         TransportMode::Radiance, MaskP(true));
    bool backOk = true;
    for (size_t i = 0; i < PACKET_WIDTH; ++i)
        if (std::fabs(lane(pdfB, i)) > 1e-6f) backOk = false;
    Check(backOk, "Pdf == 0 on the back side");
}

// Mean per-lane difference between the sampled lobe and kd/pi.
float MeanLobeRatio(const MatteMaterial &m) {
    const DifferentialGeometryP dg = MakeDG();
    const SpectrumWavelengthsP sw = MakeSW();
    const Vector3fP wo = MakeWo();

    BSDFSampleP s;
    m.SampleF(sw, wo, dg, FloatP(0.37f), FloatP(0.61f), FloatP(0.5f),
              &s, TransportMode::Radiance, MaskP(true));

    // kd for the constant color at this SW.
    ConstantColorTexture kd(
        RGBColorP(FloatP(0.8f), FloatP(0.8f), FloatP(0.8f)));
    SWCSpectrumP kdS = kd.Evaluate(dg, sw, MaskP(true));

    float sum = 0.f;
    for (size_t i = 0; i < PACKET_WIDTH; ++i) {
        float lobe = lane(s.f, i);
        float ref = lane(kdS, i) * INVPI;
        sum += (lobe - ref);
    }
    return sum / float(PACKET_WIDTH);
}

void CheckLambertian() {
    auto m = MakeMatte(0.f);
    float diff = MeanLobeRatio(*m);
    Check(std::fabs(diff) < 1e-4f,
          "sigma==0 lobe equals kd/pi (Lambertian)");
}

void CheckOrenNayar() {
    auto m = MakeMatte(35.f);
    float diff = MeanLobeRatio(*m);
    // At sigma>0 the Oren--Nayar lobe must differ from the Lambertian kd/pi.
    Check(std::fabs(diff) > 1e-3f,
          "sigma>0 lobe deviates from kd/pi (Oren--Nayar active)");
}

// -----------------------------------------------------------------------
// D.2 metal2 (Schlick microfacet + FULL FresnelGeneral).
// -----------------------------------------------------------------------
std::shared_ptr<Metal2Material> MakeMetal2(float rough) {
    auto kr = std::make_shared<ConstantColorTexture>(
        RGBColorP(FloatP(0.7f), FloatP(0.7f), FloatP(0.7f)));
    auto fr = std::make_shared<FresnelColorTexture>(kr);
    auto nu = std::make_shared<ConstantFloatTexture>(FloatP(rough));
    auto nv = std::make_shared<ConstantFloatTexture>(FloatP(rough));
    return std::make_shared<Metal2Material>(fr, nu, nv);
}

void CheckMetal2Flags() {
    auto m = MakeMetal2(0.01f);
    const uint32_t expected = uint32_t(BSDFType::GlossyReflection) |
                              uint32_t(BSDFType::FrontSide);
    Check(m->flags() == expected, "metal2 flags == GlossyReflection | FrontSide");
}

void CheckMetal2Sample() {
    auto m = MakeMetal2(0.01f);
    const DifferentialGeometryP dg = MakeDG();
    const SpectrumWavelengthsP sw = MakeSW();
    const Vector3fP wo = MakeWoRefl();

    BSDFSampleP s;
    s.pdf = FloatP(-999.f);
    m->SampleF(sw, wo, dg, FloatP(0.31f), FloatP(0.72f), FloatP(0.5f),
               &s, TransportMode::Radiance, MaskP(true));

    bool pdfPos = true, fFinite = true, typeOk = true, specOk = true, hemiOk = true;
    for (size_t i = 0; i < PACKET_WIDTH; ++i) {
        if (!(lane(s.pdf, i) > 0.f)) pdfPos = false;
        float x = lane(s.f, i);
        if (!(x >= 0.f) || !(x < 1e30f)) fFinite = false;
        if (!(lane(s.wo, 2)[i] > 0.f)) hemiOk = false;
        if (lane(s.sampledType, i) != uint32_t(BSDFType::GlossyReflection))
            typeOk = false;
        if (lane(s.specular, i)) specOk = false;
    }
    Check(pdfPos, "metal2 SampleF pdf > 0 on every lane");
    Check(fFinite, "metal2 SampleF f finite and non-negative");
    Check(hemiOk, "metal2 SampleF wi on the +n hemisphere");
    Check(typeOk, "metal2 SampleF sampledType == GlossyReflection");
    Check(specOk, "metal2 SampleF specular == false");
}

// Pdf(wo, wi) must reproduce the pdf SampleF returned (both D(wh)/(4|cosTH|)).
void CheckMetal2PdfConsistency() {
    auto m = MakeMetal2(0.01f);
    const DifferentialGeometryP dg = MakeDG();
    const SpectrumWavelengthsP sw = MakeSW();
    const Vector3fP wo = MakeWoRefl();

    BSDFSampleP s;
    m->SampleF(sw, wo, dg, FloatP(0.31f), FloatP(0.72f), FloatP(0.5f),
               &s, TransportMode::Radiance, MaskP(true));

    FloatP pdfEval = m->Pdf(sw, s.wo, wo, dg, uint32_t(BSDFType::All),
                            TransportMode::Radiance, MaskP(true));

    bool ok = true;
    for (size_t i = 0; i < PACKET_WIDTH; ++i) {
        float a = lane(s.pdf, i), b = lane(pdfEval, i);
        if (!(b > 0.f)) ok = false;
        if (std::fabs(a - b) > 1e-3f * std::max(1.f, std::fabs(a))) ok = false;
    }
    Check(ok, "metal2 Pdf(wo,wi) matches SampleF pdf (D(wh)/4|cosTH|)");
}

// At normal incidence the FULL model must reduce to FrCond (plan §2.4).
void CheckMetal2FresnelNormal() {
    const SWCSpectrumP c(FloatP(0.7f));
    FresnelGeneralP fg;
    fg.eta = FresnelApproxEta(c);
    fg.k = FresnelApproxK(c);
    fg.model = FresnelModel::Full;

    SWCSpectrumP full = FresnelGeneralEvaluate(fg, FloatP(1.f));
    SWCSpectrumP cond = FrCond(FloatP(1.f), fg.eta, fg.k);

    bool ok = true;
    for (size_t i = 0; i < PACKET_WIDTH; ++i) {
        float a = lane(full, i), b = lane(cond, i);
        if (std::fabs(a - b) > 1e-3f * std::max(1.f, std::fabs(b))) ok = false;
    }
    Check(ok, "metal2 FULL Fresnel == FrCond at normal incidence");
}

// -----------------------------------------------------------------------
// D.3 glass (specular dielectric, Fresnel-importance-sampled lobes).
// -----------------------------------------------------------------------
std::shared_ptr<GlassMaterial> MakeGlass(float index) {
    auto kr = std::make_shared<ConstantColorTexture>(
        RGBColorP(FloatP(0.8f), FloatP(0.8f), FloatP(0.8f)));
    auto kt = std::make_shared<ConstantColorTexture>(
        RGBColorP(FloatP(0.8f), FloatP(0.8f), FloatP(0.8f)));
    auto idx = std::make_shared<ConstantFloatTexture>(FloatP(index));
    return std::make_shared<GlassMaterial>(kr, kt, idx);
}

void CheckGlassFlags() {
    auto m = MakeGlass(1.5f);
    const uint32_t expected = uint32_t(BSDFType::SpecularReflection) |
                              uint32_t(BSDFType::SpecularTransmission) |
                              uint32_t(BSDFType::FrontSide) |
                              uint32_t(BSDFType::BackSide);
    Check(m->flags() == expected,
          "glass flags == SpecRefl | SpecTrans | Front | Back");
}

// Force a lobe with the coin (u2): u2=0 -> reflect (F>0), u2=1 -> transmit (F<1).
void CheckGlassNormal() {
    auto m = MakeGlass(1.5f);
    const DifferentialGeometryP dg = MakeDG();
    const SpectrumWavelengthsP sw = MakeSW();
    // Entering: wo on the +n side.
    const Vector3fP wo = enoki::normalize(Vector3fP(FloatP(0.1f), FloatP(0.f), FloatP(1.f)));

    // Reflect (u2 = 0): pdf == F ~= ((1.5-1)/(1.5+1))^2 = 0.04.
    BSDFSampleP sr;
    m->SampleF(sw, wo, dg, FloatP(0.f), FloatP(0.f), FloatP(0.f),
               &sr, TransportMode::Radiance, MaskP(true));
    bool reflType = true, reflPdf = true, reflSpec = true;
    for (size_t i = 0; i < PACKET_WIDTH; ++i) {
        if (lane(sr.sampledType, i) != uint32_t(BSDFType::SpecularReflection))
            reflType = false;
        if (std::fabs(lane(sr.pdf, i) - 0.04f) > 1e-3f) reflPdf = false;
        if (!lane(sr.specular, i)) reflSpec = false;
    }
    Check(reflType, "glass normal u2=0 -> SpecularReflection");
    Check(reflPdf, "glass normal reflect pdf == F ~= 0.04");
    Check(reflSpec, "glass specular == true");

    // Transmit (u2 = 1): pdf == 1 - F ~= 0.96.
    BSDFSampleP st;
    m->SampleF(sw, wo, dg, FloatP(0.f), FloatP(0.f), FloatP(1.f),
               &st, TransportMode::Radiance, MaskP(true));
    bool transType = true, transPdf = true, transFinite = true;
    for (size_t i = 0; i < PACKET_WIDTH; ++i) {
        if (lane(st.sampledType, i) != uint32_t(BSDFType::SpecularTransmission))
            transType = false;
        if (std::fabs(lane(st.pdf, i) - 0.96f) > 1e-3f) transPdf = false;
        float x = lane(st.f, i);
        if (!(x >= 0.f) || !(x < 1e30f)) transFinite = false;
    }
    Check(transType, "glass normal u2=1 -> SpecularTransmission");
    Check(transPdf, "glass normal transmit pdf == 1-F ~= 0.96");
    Check(transFinite, "glass transmit f finite and non-negative");
}

// Grazing incidence: F -> 1, so the reflect coin (u2 < F) fires for u2 near 1.
void CheckGlassGrazing() {
    auto m = MakeGlass(1.5f);
    const DifferentialGeometryP dg = MakeDG();
    const SpectrumWavelengthsP sw = MakeSW();
    // Entering, very grazing (wo.z small positive).
    const Vector3fP wo = enoki::normalize(Vector3fP(FloatP(1.f), FloatP(0.f), FloatP(0.02f)));

    // Force the reflect branch (u2 = 0) and check its pdf (= F) is high,
    // demonstrating F -> 1 at grazing incidence.
    BSDFSampleP s;
    m->SampleF(sw, wo, dg, FloatP(0.f), FloatP(0.f), FloatP(0.f),
               &s, TransportMode::Radiance, MaskP(true));
    bool ok = true;
    for (size_t i = 0; i < PACKET_WIDTH; ++i)
        if (!(lane(s.pdf, i) > 0.85f)) ok = false;
    Check(ok, "glass grazing -> reflect pdf (F) high (F -> 1)");
}

// Total internal reflection: ray inside (wo on -n side) beyond the critical
// angle -> F == 1, forced reflect regardless of coin.
void CheckGlassTIR() {
    auto m = MakeGlass(1.5f);
    const DifferentialGeometryP dg = MakeDG();
    const SpectrumWavelengthsP sw = MakeSW();
    // Not entering (wo.z < 0), grazing enough that eta^2 * sin^2 >= 1.
    const Vector3fP wo = enoki::normalize(Vector3fP(FloatP(0.9f), FloatP(0.f), FloatP(-0.4f)));

    BSDFSampleP s;
    m->SampleF(sw, wo, dg, FloatP(0.f), FloatP(0.f), FloatP(0.5f),
               &s, TransportMode::Radiance, MaskP(true));
    bool typeOk = true, pdfOk = true;
    for (size_t i = 0; i < PACKET_WIDTH; ++i) {
        if (lane(s.sampledType, i) != uint32_t(BSDFType::SpecularReflection))
            typeOk = false;
        if (std::fabs(lane(s.pdf, i) - 1.f) > 1e-3f) pdfOk = false;
    }
    Check(typeOk, "glass TIR -> SpecularReflection");
    Check(pdfOk, "glass TIR pdf == 1 (F == 1)");
}

} // namespace

int main() {
    std::cout << "lux2 material check (D.1 matte, D.2 metal2, D.3 glass)\n";
    CheckFlags();
    CheckSample();
    CheckPdf();
    CheckLambertian();
    CheckOrenNayar();
    CheckMetal2Flags();
    CheckMetal2Sample();
    CheckMetal2PdfConsistency();
    CheckMetal2FresnelNormal();
    CheckGlassFlags();
    CheckGlassNormal();
    CheckGlassGrazing();
    CheckGlassTIR();

    if (g_failures == 0) {
        std::cout << "ALL MATERIAL CHECKS PASSED\n";
        return 0;
    }
    std::cout << g_failures << " MATERIAL CHECK(S) FAILED\n";
    return 1;
}
