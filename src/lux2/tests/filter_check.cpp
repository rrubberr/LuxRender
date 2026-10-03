/***************************************************************************
 *   This file is part of LuxRender.                                       *
 *                                                                         *
 *   To the extent possible under law, the author(s) have dedicated all    *
 *   copyright and related neighboring rights to the belowe code to the    *
 *   public domain worldwide. The below code is distributed without any    *
 *   warranty.                                                             *
 *                                                                         *
 *   See: <https://creativecommons.org/publicdomain/zero/1.0/>             *
 *                                                                         *
 ***************************************************************************/
// Tests are machine generated. Proceed with caution!

// Verifies the Gaussian filter.

#include "filters/gaussian.h"

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

// Scalar reference (lux formula).
float RefGaussian(float d, float alpha, float width) {
    const float expv = std::exp(-alpha * width * width);
    return std::max(0.f, std::exp(-alpha * d * d) - expv);
}

} // namespace

int main() {
    std::cout << "lux2 filter_check (Stage 3)" << std::endl;

    const float xw = 2.f, yw = 2.f, alpha = 2.f;
    GaussianFilter f(xw, yw, alpha);

    Check(std::abs(f.GetXWidth() - xw) < 1e-6f, "GetXWidth == xwidth");
    Check(std::abs(f.GetYWidth() - yw) < 1e-6f, "GetYWidth == ywidth");

    const float c = f.Evaluate(0.f, 0.f);
    Check(c > 0.f, "Evaluate(0,0) > 0");

    // Center is the maximum.
    bool centerMax = true;
    for (float d = 0.1f; d < xw; d += 0.1f) {
        if (!(f.Evaluate(d, 0.f) < c)) centerMax = false;
        if (!(f.Evaluate(0.f, d) < c)) centerMax = false;
    }
    Check(centerMax, "center is the maximum");

    // Symmetry.
    bool sym = true;
    for (float d = 0.f; d < xw; d += 0.13f) {
        if (std::abs(f.Evaluate(d, 0.f) - f.Evaluate(-d, 0.f)) > 1e-6f) sym = false;
        if (std::abs(f.Evaluate(0.f, d) - f.Evaluate(0.f, -d)) > 1e-6f) sym = false;
    }
    Check(sym, "symmetric in x and y");

    // Separability: Evaluate(dx,dy) == Evaluate(dx,0)*Evaluate(0,dy)/Evaluate(0,0).
    bool sep = true;
    for (float dx = 0.f; dx < xw; dx += 0.21f)
        for (float dy = 0.f; dy < yw; dy += 0.21f) {
            const float lhs = f.Evaluate(dx, dy);
            const float rhs = f.Evaluate(dx, 0.f) * f.Evaluate(0.f, dy) / c;
            if (std::abs(lhs - rhs) > 1e-5f) sep = false;
        }
    Check(sep, "separable in x and y");

    // Truncation at |d| >= width.
    Check(f.Evaluate(xw, 0.f) == 0.f, "truncates at xwidth");
    Check(f.Evaluate(0.f, yw) == 0.f, "truncates at ywidth");
    Check(f.Evaluate(xw + 0.5f, 0.f) == 0.f, "zero beyond xwidth");

    // Matches scalar reference.
    bool matches = true;
    for (float d = 0.f; d < xw; d += 0.17f) {
        const float ref = RefGaussian(d, alpha, xw) * RefGaussian(0.f, alpha, yw);
        if (std::abs(f.Evaluate(d, 0.f) - ref) > 1e-6f) matches = false;
    }
    Check(matches, "matches scalar reference gaussian");

    std::cout << (g_failures == 0 ? "ALL PASSED" : "FAILURES")
              << " (" << g_failures << ")" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
