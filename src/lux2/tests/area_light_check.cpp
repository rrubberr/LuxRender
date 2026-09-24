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

// Verifies the area light:

#include "lights/area.h"
#include "core/color.h"
#include "core/geometry.h"
#include "core/shape.h"
#include "core/vecp.h"
#include "textures/constant.h"

#include <cmath>
#include <iostream>
#include <memory>
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

TriangleDesc QuadTri(const Point3f &a, const Point3f &b, const Point3f &c) {
    TriangleDesc t;
    t.v0 = a; t.v1 = b; t.v2 = c;
    t.n0 = Normal3f(0.f, 0.f, 1.f);
    t.n1 = Normal3f(0.f, 0.f, 1.f);
    t.n2 = Normal3f(0.f, 0.f, 1.f);
    t.matID = 0; t.lightID = 0;
    return t;
}

std::shared_ptr<AreaLight> MakeLight(float gain = 1.f) {
    const RGBColorP white(FloatP(1.f), FloatP(1.f), FloatP(1.f));
    auto le = std::make_shared<ConstantColorTexture>(white);
    return std::make_shared<AreaLight>(le, gain);
}

} // namespace

int main() {
    std::cout << "lux2 area_light_check (Stage 5)" << std::endl;

    // Unit quad in the z=0 plane, split into two triangles, total area 1.
    const Point3f p00(0.f, 0.f, 0.f), p10(1.f, 0.f, 0.f);
    const Point3f p11(1.f, 1.f, 0.f), p01(0.f, 1.f, 0.f);

    // ---- 1. flags / infinite --------------------------------------------
    {
        auto light = MakeLight();
        Check(light->flags() == uint32_t(BSDFType::DiffuseReflection),
              "flags() == DiffuseReflection");
        Check(!light->IsInfinite(), "IsInfinite() == false");
        Check(light->TotalArea() == 0.f, "TotalArea() == 0 before BindGeometry");
    }

    // ---- 2. BindGeometry area + CDF -------------------------------------
    {
        auto light = MakeLight();
        std::vector<TriangleDesc> tris = { QuadTri(p00, p10, p11),
                                           QuadTri(p00, p11, p01) };
        light->BindGeometry(tris);
        Check(std::abs(light->TotalArea() - 1.f) < 1e-5f,
              "TotalArea() == 1 for unit quad");
    }

    // ---- 3/4. Sample_L lands on plane, pdf>0, Pdf_L consistency ---------
    {
        auto light = MakeLight();
        std::vector<TriangleDesc> tris = { QuadTri(p00, p10, p11),
                                           QuadTri(p00, p11, p01) };
        light->BindGeometry(tris);

        // Shading point above the quad center; its normal faces down toward
        // the emitter so the light is in the front hemisphere.
        Point3fP p(FloatP(0.5f), FloatP(0.5f), FloatP(1.f));
        Normal3fP n(FloatP(0.f), FloatP(0.f), FloatP(-1.f));

        SpectrumWavelengthsP sw;
        sw.Sample(FloatP(0.5f));
        FloatP u0(FloatP(0.25f)), u1(FloatP(0.5f)), u2(FloatP(0.5f));

        Point3fP lightP; Vector3fP wi; FloatP pdf; SWCSpectrumP Le;
        MaskP valid = light->Sample_L(sw, p, n, u0, u1, u2,
                                      &lightP, &wi, &pdf, &Le);

        Check(bool(enoki::all(valid)), "Sample_L valid for facing point");
        // lightP.z should be ~0 (on the emitter plane).
        Check(std::abs(float(lightP.z()[0])) < 1e-4f,
              "Sample_L lands on z==0 plane");
        // lightP within the unit square.
        Check(lightP.x()[0] >= -1e-4f && lightP.x()[0] <= 1.f + 1e-4f &&
              lightP.y()[0] >= -1e-4f && lightP.y()[0] <= 1.f + 1e-4f,
              "Sample_L lands inside the quad");
        Check(pdf[0] > 0.f, "Sample_L pdf > 0");

        // Pdf_L at the same lightP must match Sample_L's pdf.
        Normal3fP lightN(FloatP(0.f), FloatP(0.f), FloatP(1.f));
        FloatP pdfL = light->Pdf_L(p, n, lightP, lightN);
        Check(std::abs(pdfL[0] - pdf[0]) < 1e-3f * (pdf[0] + 1e-6f),
              "Pdf_L matches Sample_L pdf");
    }

    // ---- 5. backfacing shading point -> pdf 0 ---------------------------
    {
        auto light = MakeLight();
        std::vector<TriangleDesc> tris = { QuadTri(p00, p10, p11),
                                           QuadTri(p00, p11, p01) };
        light->BindGeometry(tris);

        // Point below the plane, normal pointing away (down): the quad is
        // behind the shading surface, cosShading < 0.
        Point3fP p(FloatP(0.5f), FloatP(0.5f), FloatP(-1.f));
        Normal3fP n(FloatP(0.f), FloatP(0.f), FloatP(1.f));

        SpectrumWavelengthsP sw;
        sw.Sample(FloatP(0.5f));
        Point3fP lightP; Vector3fP wi; FloatP pdf; SWCSpectrumP Le;
        light->Sample_L(sw, p, n, FloatP(0.25f), FloatP(0.5f), FloatP(0.5f),
                        &lightP, &wi, &pdf, &Le);
        Check(pdf[0] == 0.f, "Sample_L pdf == 0 for backfacing point");
    }

    // ---- 6. every triangle reachable ------------------------------------
    {
        // A tall thin quad split into two triangles of very different area:
        // one area 1 (u in [0,1]) and one area 0.25. Sampling across u0 must
        // hit both.
        auto light = MakeLight();
        const Point3f a(0.f, 0.f, 0.f), b(1.f, 0.f, 0.f);
        const Point3f c(1.f, 1.f, 0.f), d(0.f, 1.f, 0.f);
        std::vector<TriangleDesc> tris = { QuadTri(a, b, c), QuadTri(a, c, d) };
        light->BindGeometry(tris);

        Point3fP p(FloatP(0.5f), FloatP(0.5f), FloatP(1.f));
        Normal3fP n(FloatP(0.f), FloatP(0.f), FloatP(1.f));
        SpectrumWavelengthsP sw;
        sw.Sample(FloatP(0.5f));

        bool hitLow = false, hitHigh = false;
        for (int i = 0; i < 64; ++i) {
            FloatP u0(FloatP((i + 0.5f) / 64.f));
            Point3fP lightP; Vector3fP wi; FloatP pdf; SWCSpectrumP Le;
            light->Sample_L(sw, p, n, u0, FloatP(0.5f), FloatP(0.5f),
                            &lightP, &wi, &pdf, &Le);
            // Triangle 0 (a,b,c) has centroid (2/3,1/3); triangle 1 (a,c,d)
            // has centroid (1/3,2/3). Distinguish by x vs y.
            if (lightP.x()[0] > lightP.y()[0]) hitLow = true;
            else hitHigh = true;
        }
        Check(hitLow && hitHigh, "Both triangles reachable across u0");
    }

    std::cout << (g_failures == 0 ? "ALL PASS" : "FAILURES")
              << " (" << g_failures << ")" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
