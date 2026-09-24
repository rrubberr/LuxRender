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

// Verifies the Phase-1 texture plugins and the Fresnel math they rely on.
//
//   1. ConstantFloatTexture / ConstantColorTexture broadcast + IsConstant.
//   2. ConstantFresnelTexture yields a dielectric {eta=value, k=0}.
//   3. FresnelApproxEta/K at Fr=0.7 match the analytic values (~5.8, ~3.06).
//   4. FrCond at normal incidence reproduces ((eta-1)^2+k^2)/((eta+1)^2+k^2).
//   5. fresnelcolor maps a constant color to a FULL-model FresnelGeneralP.

#include "core/texture.h"
#include "core/fresnel.h"
#include "core/spectrum.h"
#include "core/vecp.h"
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

bool Close(float a, float b, float tol = 1e-3f) {
    return std::fabs(a - b) <= tol * std::max(1.f, std::max(std::fabs(a), std::fabs(b)));
}

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

// A properly-sampled SpectrumWavelengthsP (populates the Smits bins/offsets).
SpectrumWavelengthsP MakeSW() {
    SpectrumWavelengthsP sw;
    sw.Sample(FloatP(0.5f));
    return sw;
}

// -----------------------------------------------------------------------
// 1. Constant float / color.
// -----------------------------------------------------------------------
void CheckConstantFloat() {
    ConstantFloatTexture tex(FloatP(0.42f));
    FloatP v = tex.Evaluate(MakeDG(), MakeSW(), MaskP(true));
    bool all42 = enoki::all(v == FloatP(0.42f));
    Check(all42, "ConstantFloatTexture broadcasts 0.42 to every lane");
    Check(tex.IsConstant(), "ConstantFloatTexture IsConstant() == true");
}

void CheckConstantColor() {
    const RGBColorP rgb(FloatP(0.2f), FloatP(0.3f), FloatP(0.4f));
    ConstantColorTexture tex(rgb);
    Check(tex.IsConstant(), "ConstantColorTexture IsConstant() == true");

    // Evaluate at a fixed wavelength set; every wavelength sample of the
    // Smits reconstruction must be finite and non-negative.
    SWCSpectrumP s = tex.Evaluate(MakeDG(), MakeSW(), MaskP(true));
    bool finite = true;
    for (int i = 0; i < WAVELENGTH_SAMPLES; ++i) {
        for (size_t lane = 0; lane < PACKET_WIDTH; ++lane) {
            float x = s[i][lane];
            if (!(x >= 0.f) || !(x < 1e30f)) finite = false;
        }
    }
    Check(finite, "ConstantColorTexture Smits SPD is finite and non-negative");
}

// -----------------------------------------------------------------------
// 2. Constant fresnel.
// -----------------------------------------------------------------------
void CheckConstantFresnel() {
    ConstantFresnelTexture tex(FloatP(1.5f));
    FresnelGeneralP f = tex.Evaluate(MakeDG(), MakeSW(), MaskP(true));
    Check(f.model == FresnelModel::Dielectric,
          "ConstantFresnelTexture model == Dielectric");
    bool etaOk = enoki::all(f.eta == SWCSpectrumP(FloatP(1.5f)));
    bool kOk = enoki::all(f.k == SWCSpectrumP(FloatP(0.f)));
    Check(etaOk, "ConstantFresnelTexture eta == 1.5 across wavelengths");
    Check(kOk, "ConstantFresnelTexture k == 0 across wavelengths");
    Check(tex.IsConstant(), "ConstantFresnelTexture IsConstant() == true");
}

// -----------------------------------------------------------------------
// 3. FresnelApproxEta/K at Fr=0.7.
// -----------------------------------------------------------------------
void CheckApproxEtaK() {
    const SWCSpectrumP Fr(FloatP(0.7f));
    SWCSpectrumP eta = FresnelApproxEta(Fr);
    SWCSpectrumP k = FresnelApproxK(Fr);

    const float etaRef = (1.f + std::sqrt(0.7f)) / (1.f - std::sqrt(0.7f));
    const float kRef = 2.f * std::sqrt(0.7f / 0.3f);

    bool etaOk = true, kOk = true;
    for (int i = 0; i < WAVELENGTH_SAMPLES; ++i) {
        if (!Close(eta[i][0], etaRef)) etaOk = false;
        if (!Close(k[i][0], kRef)) kOk = false;
    }
    std::cout << "    eta(0.7)=" << eta[0][0] << " (ref " << etaRef << ")  "
              << "k(0.7)=" << k[0][0] << " (ref " << kRef << ")\n";
    Check(etaOk, "FresnelApproxEta(0.7) matches analytic");
    Check(kOk, "FresnelApproxK(0.7) matches analytic");
}

// -----------------------------------------------------------------------
// 4. FrCond at normal incidence.
// -----------------------------------------------------------------------
void CheckFrCondNormal() {
    const SWCSpectrumP eta(FloatP(5.7983f));
    const SWCSpectrumP k(FloatP(3.0551f));
    const FloatP cosi(1.f);
    SWCSpectrumP R = FrCond(cosi, eta, k);

    const float e = 5.7983f, kk = 3.0551f;
    const float ref = ((e - 1.f) * (e - 1.f) + kk * kk) /
                      ((e + 1.f) * (e + 1.f) + kk * kk);
    bool ok = true;
    for (int i = 0; i < WAVELENGTH_SAMPLES; ++i)
        if (!Close(R[i][0], ref, 2e-3f)) ok = false;
    std::cout << "    FrCond(1)=" << R[0][0] << " (ref " << ref << ")\n";
    Check(ok, "FrCond at normal incidence matches analytic conductor R");
}

// -----------------------------------------------------------------------
// 5. fresnelcolor -> FULL-model FresnelGeneralP.
// -----------------------------------------------------------------------
void CheckFresnelColor() {
    // A gray constant color stands in for the Kr color texture.
    auto gray = std::make_shared<ConstantColorTexture>(
        RGBColorP(FloatP(0.7f), FloatP(0.7f), FloatP(0.7f)));

    FresnelColorTexture fc(gray);
    FresnelGeneralP f = fc.Evaluate(MakeDG(), MakeSW(), MaskP(true));
    Check(f.model == FresnelModel::Full,
          "fresnelcolor yields a FULL-model FresnelGeneralP");

    // At a wavelength where the Smits reconstruction of gray 0.7 is ~0.7,
    // eta/k should be near the analytic ApproxEta/K(0.7). Just assert the
    // relationship eta == ApproxEta(c), k == ApproxK(c) is self-consistent
    // by recomputing from the evaluated color.
    SWCSpectrumP c = gray->Evaluate(MakeDG(), MakeSW(), MaskP(true));
    SWCSpectrumP etaRef = FresnelApproxEta(c);
    SWCSpectrumP kRef = FresnelApproxK(c);
    bool etaOk = true, kOk = true;
    for (int i = 0; i < WAVELENGTH_SAMPLES; ++i) {
        for (size_t lane = 0; lane < PACKET_WIDTH; ++lane) {
            if (!Close(f.eta[i][lane], etaRef[i][lane])) etaOk = false;
            if (!Close(f.k[i][lane], kRef[i][lane])) kOk = false;
        }
    }
    Check(etaOk, "fresnelcolor eta == FresnelApproxEta(color)");
    Check(kOk, "fresnelcolor k == FresnelApproxK(color)");
}

} // namespace

int main() {
    std::cout << "lux2 texture_check (PACKET_WIDTH=" << PACKET_WIDTH << ")\n";

    CheckConstantFloat();
    CheckConstantColor();
    CheckConstantFresnel();
    CheckApproxEtaK();
    CheckFrCondNormal();
    CheckFresnelColor();

    if (g_failures == 0) {
        std::cout << "lux2texturecheck: ALL CHECKS PASSED\n";
        return 0;
    }
    std::cerr << "lux2texturecheck: " << g_failures << " FAILURE(S)\n";
    return 1;
}
