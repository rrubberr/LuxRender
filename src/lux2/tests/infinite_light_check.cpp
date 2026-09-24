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

// Verifies the infinite light.

#include "lights/infinite.h"
#include "core/color.h"
#include "core/geometry.h"
#include "core/math.h"
#include "core/vecp.h"
#include "textures/constant.h"

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

std::shared_ptr<InfiniteLight> MakeLight(float gain = 1.f) {
    const RGBColorP white(FloatP(1.f), FloatP(1.f), FloatP(1.f));
    auto le = std::make_shared<ConstantColorTexture>(white);
    return std::make_shared<InfiniteLight>(le, gain);
}

} // namespace

int main() {
    std::cout << "lux2 infinite_light_check (Stage 6)" << std::endl;

    // ---- 1. flags / infinite --------------------------------------------
    {
        auto light = MakeLight();
        Check(light->flags() == uint32_t(BSDFType::DiffuseReflection),
              "flags() == DiffuseReflection");
        Check(light->IsInfinite(), "IsInfinite() == true");
    }

    // ---- 2/3. Sample_L: unit dir, pdf == 1/(4pi), sphere coverage -------
    {
        auto light = MakeLight();
        Point3fP p(FloatP(0.f), FloatP(0.f), FloatP(0.f));
        Normal3fP n(FloatP(0.f), FloatP(0.f), FloatP(1.f));
        SpectrumWavelengthsP sw;
        sw.Sample(FloatP(0.5f));

        bool anyPosZ = false, anyNegZ = false, anyPosX = false, anyNegX = false;
        bool allUnit = true, allPdf = true;
        for (int i = 0; i < 64; ++i) {
            FloatP u0(FloatP((i + 0.5f) / 64.f));
            FloatP u1(FloatP(std::fmod(i * 0.6180339887f, 1.f)));
            Point3fP lightP; Vector3fP wi; FloatP pdf; SWCSpectrumP Le;
            light->Sample_L(sw, p, n, u0, u1, FloatP(0.f),
                            &lightP, &wi, &pdf, &Le);
            const FloatP len = enoki::sqrt(enoki::dot(wi, wi));
            if (std::abs(len[0] - 1.f) > 1e-4f) allUnit = false;
            if (std::abs(pdf[0] - INV_FOURPI) > 1e-6f) allPdf = false;
            if (wi.z()[0] > 0.1f) anyPosZ = true;
            if (wi.z()[0] < -0.1f) anyNegZ = true;
            if (wi.x()[0] > 0.1f) anyPosX = true;
            if (wi.x()[0] < -0.1f) anyNegX = true;
        }
        Check(allUnit, "Sample_L directions are unit length");
        Check(allPdf, "Sample_L pdf == 1/(4*PI)");
        Check(anyPosZ && anyNegZ && anyPosX && anyNegX,
              "Sampled directions cover the sphere");
    }

    // ---- 4/5. Pdf_L constant, matches Sample_L --------------------------
    {
        auto light = MakeLight();
        Point3fP p(FloatP(1.f), FloatP(2.f), FloatP(3.f));
        Normal3fP n(FloatP(0.f), FloatP(0.f), FloatP(1.f));
        Point3fP lp(FloatP(9.f), FloatP(9.f), FloatP(9.f));
        Normal3fP ln(FloatP(0.f), FloatP(1.f), FloatP(0.f));
        FloatP pdfL = light->Pdf_L(p, n, lp, ln);
        Check(std::abs(pdfL[0] - INV_FOURPI) < 1e-6f,
              "Pdf_L == 1/(4*PI)");

        // Independent of position/normal.
        FloatP pdfL2 = light->Pdf_L(Point3fP(FloatP(-5.f), FloatP(0.f), FloatP(0.f)),
                                    Normal3fP(FloatP(1.f), FloatP(0.f), FloatP(0.f)),
                                    lp, ln);
        Check(std::abs(pdfL2[0] - INV_FOURPI) < 1e-6f,
              "Pdf_L independent of position/normal");
    }

    std::cout << (g_failures == 0 ? "ALL PASS" : "FAILURES")
              << " (" << g_failures << ")" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
