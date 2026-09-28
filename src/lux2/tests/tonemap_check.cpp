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

// Verifies the linear/autolinear tonemap kernels and the registry.

#include "core/paramset.h"
#include "core/tonemap.h"
#include "tonemaps/lineartonemap.h"

#include <cmath>
#include <iostream>
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

bool RelClose(float a, float b, float tol) {
    return std::fabs(a - b) <= tol * (1.f + std::fabs(a) + std::fabs(b));
}

void CheckLinearOp() {
    const float sensitivity = 50.f, exposure = 1.f, fstop = 2.8f, gamma = 1.f;
    ParamSet ps;
    ps.AddFloat("sensitivity", &sensitivity);
    ps.AddFloat("exposure", &exposure);
    ps.AddFloat("fstop", &fstop);
    ps.AddFloat("gamma", &gamma);

    auto tm = MakeToneMap("linear", ps);
    Check(tm != nullptr, "MakeToneMap(\"linear\") non-null");
    if (!tm)
        return;

    const float expected = exposure / (fstop * fstop) * sensitivity * 0.65f /
                           10.f * std::pow(118.f / 255.f, gamma);

    const int xRes = 4, yRes = 4;
    std::vector<XYZColor> xyz(xRes * yRes, XYZColor(0.4f, 0.5f, 0.6f));
    tm->Map(xyz, xRes, yRes, 100.f);

    // Uniform scaling by the exact factor.
    const bool ok = RelClose(xyz[0][0], 0.4f * expected, 1e-6f) &&
                    RelClose(xyz[0][1], 0.5f * expected, 1e-6f) &&
                    RelClose(xyz[0][2], 0.6f * expected, 1e-6f);
    Check(ok, "LinearOp scales XYZ uniformly by exact factor");
}

void CheckEVOp() {
    const float gamma = 2.2f;
    ParamSet ps;
    ps.AddFloat("gamma", &gamma);

    auto tm = MakeToneMap("autolinear", ps);
    Check(tm != nullptr, "MakeToneMap(\"autolinear\") non-null");
    if (!tm)
        return;

    // Mixed image: some positive, some zero/negative Y.
    const int xRes = 5, yRes = 5;
    std::vector<XYZColor> xyz;
    xyz.push_back(XYZColor(0.1f, 0.2f, 0.3f));
    xyz.push_back(XYZColor(0.0f, 0.0f, 0.0f));
    xyz.push_back(XYZColor(-0.5f, -0.1f, 0.f));
    xyz.push_back(XYZColor(0.3f, 0.8f, 0.1f));
    xyz.push_back(XYZColor(0.2f, 0.4f, 0.05f));
    while (int(xyz.size()) < xRes * yRes)
        xyz.push_back(XYZColor(0.f, 0.f, 0.f));

    // Mean Y over strictly-positive pixels before mapping.
    float sum = 0.f;
    int n = 0;
    for (const auto &c : xyz) {
        if (c.Y() > 0.f) {
            sum += c.Y();
            ++n;
        }
    }
    const float ybarBefore = sum / float(n);

    tm->Map(xyz, xRes, yRes, 100.f);

    // After mapping, mean Y over positive pixels == 1.25 * (118/255)^gamma.
    float sumAfter = 0.f;
    int nAfter = 0;
    for (const auto &c : xyz) {
        if (c.Y() > 0.f) {
            sumAfter += c.Y();
            ++nAfter;
        }
    }
    const float meanAfter = sumAfter / float(nAfter);
    const float target = 1.25f * std::pow(118.f / 255.f, gamma);
    Check(std::fabs(meanAfter - target) <= 1e-4f * (1.f + target),
          "EVOp normalizes mean positive Y to 1.25*(118/255)^gamma");

    // Negative/zero pixels must remain non-positive (factor > 0).
    Check(xyz[2].Y() <= 0.f, "EVOp leaves negative-Y pixels non-positive");
    (void)ybarBefore;
}

void CheckEVOpAllZero() {
    const float gamma = 2.2f;
    ParamSet ps;
    ps.AddFloat("gamma", &gamma);
    auto tm = MakeToneMap("autolinear", ps);
    if (!tm) {
        Check(false, "MakeToneMap(\"autolinear\") for all-zero case");
        return;
    }

    const int xRes = 3, yRes = 3;
    std::vector<XYZColor> xyz(xRes * yRes, XYZColor(0.f, 0.f, 0.f));
    tm->Map(xyz, xRes, yRes, 100.f);

    bool unchanged = true;
    for (const auto &c : xyz)
        if (c[0] != 0.f || c[1] != 0.f || c[2] != 0.f)
            unchanged = false;
    Check(unchanged, "EVOp all-zero image is a no-op");
}

void CheckRegistry() {
    Check(MakeToneMap("linear", ParamSet()) != nullptr,
          "registry: linear constructible with default params");
    Check(MakeToneMap("autolinear", ParamSet()) != nullptr,
          "registry: autolinear constructible with default params");
    Check(MakeToneMap("does_not_exist", ParamSet()) == nullptr,
          "registry: unknown name returns null");
}

} // namespace

int main() {
    std::cout << "lux2 tonemap_check (Stage D)" << std::endl;

    CheckLinearOp();
    CheckEVOp();
    CheckEVOpAllZero();
    CheckRegistry();

    if (g_failures == 0) {
        std::cout << "ALL TONEMAP CHECKS PASSED" << std::endl;
        return 0;
    }
    std::cerr << g_failures << " tonemap check(s) FAILED" << std::endl;
    return 1;
}
