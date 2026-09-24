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

// Verifies the perspective camera.

#include "cameras/perspective.h"
#include "core/ray.h"
#include "core/transform.h"
#include "core/vecp.h"

#include <cmath>
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

// Build an identity-transform camera, fov 90, square 100x100 film.
PerspectiveCamera MakeCamera() {
    const float screen[4] = { -1.f, 1.f, -1.f, 1.f };
    return PerspectiveCamera(Transform(), screen, 1e-3f, 1e30f, 0.f,
                             100, 100, 90.f);
}

} // namespace

int main() {
    std::cout << "lux2 camera_check (Stage 4)" << std::endl;

    PerspectiveCamera cam = MakeCamera();

    Check(cam.PixelWidth() == 100, "PixelWidth == 100");
    Check(cam.PixelHeight() == 100, "PixelHeight == 100");

    // ---- 1. center pixel -> +Z ------------------------------------------
    {
        RayP ray;
        FloatP weight;
        cam.GenerateRay(FloatP(50.f), FloatP(50.f), FloatP(0.f), &ray, &weight);
        // dir should be (0,0,1).
        bool ok = std::abs(float(ray.d.x()[0])) < 1e-5f &&
                  std::abs(float(ray.d.y()[0])) < 1e-5f &&
                  std::abs(float(ray.d.z()[0]) - 1.f) < 1e-5f;
        Check(ok, "center pixel ray points along +Z");
        // origin at camera (0,0,0).
        bool originOk = std::abs(float(ray.o.x()[0])) < 1e-5f &&
                        std::abs(float(ray.o.z()[0])) < 1e-5f;
        Check(originOk, "ray origin at camera position");
    }

    // ---- 2. corner divergence, symmetric --------------------------------
    {
        RayP tl, br;
        FloatP w;
        cam.GenerateRay(FloatP(0.f), FloatP(0.f), FloatP(0.f), &tl, &w);
        cam.GenerateRay(FloatP(100.f), FloatP(100.f), FloatP(0.f), &br, &w);
        // top-left (y=0 -> screenY=+1) has +y dir; bottom-right has -y.
        const bool tlUp = float(tl.d.y()[0]) > 1e-3f;
        const bool brDown = float(br.d.y()[0]) < -1e-3f;
        Check(tlUp && brDown, "corners diverge vertically");
        // symmetric magnitudes.
        const bool sym = std::abs(float(tl.d.y()[0]) + float(br.d.y()[0])) < 1e-4f;
        Check(sym, "vertical divergence symmetric");
        // all forward (+Z).
        const bool fwd = float(tl.d.z()[0]) > 0.f && float(br.d.z()[0]) > 0.f;
        Check(fwd, "corner rays point forward (+Z)");
    }

    // ---- 3. SampleRay matches GenerateRay at center ---------------------
    {
        RayP g, s;
        FloatP gw, spdf, sw;
        cam.GenerateRay(FloatP(50.f), FloatP(50.f), FloatP(0.f), &g, &gw);
        cam.SampleRay(FloatP(0.5f), FloatP(0.5f), FloatP(0.f), &s, &spdf, &sw);
        const bool same = std::abs(float(s.d.z()[0]) - float(g.d.z()[0])) < 1e-5f &&
                          std::abs(float(s.d.x()[0]) - float(g.d.x()[0])) < 1e-5f;
        Check(same, "SampleRay(center) matches GenerateRay(center)");
    }

    // ---- 4. Pdf ---------------------------------------------------------
    {
        // Apixel: tanHalf=tan(45)=1; xPixW=2*1*2*0.5/100=0.02; Apixel=0.0004.
        const float expectedCenter = 1.f / 0.0004f;   // cos=1 at center
        RayP center;
        FloatP w;
        cam.GenerateRay(FloatP(50.f), FloatP(50.f), FloatP(0.f), &center, &w);
        const float pdfC = float(cam.Pdf(center)[0]);
        Check(std::abs(pdfC - expectedCenter) < 1.f, "Pdf(center) == 1/Apixel");

        // Backwards ray -> 0.
        RayP back;
        back.d = Vector3fP(FloatP(0.f), FloatP(0.f), FloatP(-1.f));
        const float pdfB = float(cam.Pdf(back)[0]);
        Check(pdfB == 0.f, "Pdf(backwards ray) == 0");
    }

    std::cout << (g_failures == 0 ? "ALL PASSED" : "FAILURES")
              << " (" << g_failures << ")" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
