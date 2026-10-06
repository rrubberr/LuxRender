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

// Verifies the lux2 pixel filters against scalar references transcribed
// from the legacy (src/lux) kernels.

#include "filters/blackmanharris.h"
#include "filters/box.h"
#include "filters/catmullrom.h"
#include "filters/gaussian.h"
#include "filters/mitchell.h"
#include "filters/sinc.h"
#include "filters/triangle.h"

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

// The vectorized lanes must match the scalar kernel lane-for-lane.
void CheckVectorMatchesScalar(const Filter &f, float xw, float yw) {
    constexpr int N = lux2::PACKET_WIDTH;
    float inx[N], iny[N];
    for (int i = 0; i < N; ++i) {
        // Offsets spanning inside, at, and beyond the footprint.
        inx[i] = -1.7f * xw + 3.4f * float(i) / float(N - 1);
        iny[i] = -1.3f * yw + 2.6f * float(i) / float(N - 1);
    }
    const FloatP dx = enoki::load_unaligned<FloatP>(inx);
    const FloatP dy = enoki::load_unaligned<FloatP>(iny);
    float outx[N], outy[N];
    enoki::store_unaligned(outx, f.EvaluateXP(dx));
    enoki::store_unaligned(outy, f.EvaluateYP(dy));

    bool okX = true, okY = true;
    for (int i = 0; i < N; ++i) {
        if (std::abs(outx[i] - f.EvaluateX(inx[i])) > 1e-5f) okX = false;
        if (std::abs(outy[i] - f.EvaluateY(iny[i])) > 1e-5f) okY = false;
    }
    Check(okX, "EvaluateXP matches EvaluateX");
    Check(okY, "EvaluateYP matches EvaluateY");
}

// Check for symmetry and scalar/vector parity.
void CheckCommon(const Filter &f, float xw, float yw) {
    bool sym = true;
    for (float d = 0.f; d < xw; d += 0.13f) {
        if (std::abs(f.EvaluateX(d) - f.EvaluateX(-d)) > 1e-6f) sym = false;
        if (std::abs(f.EvaluateY(d) - f.EvaluateY(-d)) > 1e-6f) sym = false;
    }
    Check(sym, "symmetric in x and y");
    CheckVectorMatchesScalar(f, xw, yw);
}

// ---------------------------------------------------------------------------
// Scalar references (legacy formulas).
// ---------------------------------------------------------------------------

float RefTriangle(float d, float width) {
    return std::max(0.f, width - std::abs(d));
}

float RefCatmullRom(float x) {
    x = std::abs(x);
    const float x2 = x * x;
    return (x >= 2.f) ? 0.f
                      : ((x < 1.f) ? (3.f * (x * x2) - 5.f * x2 + 2.f)
                                   : (-(x * x2) + 5.f * x2 - 8.f * x + 4.f));
}

float RefBlackmanHarris1D(float x) {
    if (x < -1.f || x > 1.f)
        return 0.f;
    x = (x + 1.f) * 0.5f;
    x *= float(M_PI);
    const float A0 = 0.35875f;
    const float A1 = -0.48829f;
    const float A2 = 0.14128f;
    const float A3 = -0.01168f;
    return A0 + A1 * std::cos(2.f * x) + A2 * std::cos(4.f * x) +
           A3 * std::cos(6.f * x);
}

float RefSinc1D(float x, float tau) {
    x = std::abs(x);
    if (x < 1e-5f) return 1.f;
    if (x > 1.f)   return 0.f;
    x *= float(M_PI);
    const float sinc = std::sin(x * tau) / (x * tau);
    const float lanczos = std::sin(x) / x;
    return sinc * lanczos;
}

float RefMitchell1D(float x, float B, float C) {
    if (x >= 1.f)
        return 0.f;
    x = std::abs(2.f * x);
    if (x > 1.f)
        return (((-B / 6.f - C) * x + (B + 5.f * C)) * x +
                (-2.f * B - 8.f * C)) * x + (4.f / 3.f * B + 4.f * C);
    return ((2.f - 1.5f * B - C) * x +
            (-3.f + 2.f * B + C)) * x * x + (1.f - B / 3.f);
}

// ---------------------------------------------------------------------------
// Per-filter sections.
// ---------------------------------------------------------------------------

void CheckGaussian() {
    std::cout << "-- gaussian" << std::endl;
    const float xw = 2.f, yw = 2.f, alpha = 2.f;
    GaussianFilter f(xw, yw, alpha);

    Check(std::abs(f.GetXWidth() - xw) < 1e-6f, "GetXWidth == xwidth");
    Check(std::abs(f.GetYWidth() - yw) < 1e-6f, "GetYWidth == ywidth");

    const float c = f.Evaluate(0.f, 0.f);
    Check(c > 0.f, "Evaluate(0,0) > 0");

    bool centerMax = true;
    for (float d = 0.1f; d < xw; d += 0.1f) {
        if (!(f.Evaluate(d, 0.f) < c)) centerMax = false;
        if (!(f.Evaluate(0.f, d) < c)) centerMax = false;
    }
    Check(centerMax, "center is the maximum");

    CheckCommon(f, xw, yw);

    Check(f.Evaluate(xw, 0.f) == 0.f, "truncates at xwidth");
    Check(f.Evaluate(0.f, yw) == 0.f, "truncates at ywidth");
    Check(f.Evaluate(xw + 0.5f, 0.f) == 0.f, "zero beyond xwidth");

    const float expX = std::exp(-alpha * xw * xw);
    bool matches = true;
    for (float d = 0.f; d < xw; d += 0.17f) {
        const float ref = std::max(0.f, std::exp(-alpha * d * d) - expX);
        if (std::abs(f.EvaluateX(d) - ref) > 1e-6f) matches = false;
    }
    Check(matches, "matches scalar reference gaussian");
}

void CheckBox() {
    std::cout << "-- box" << std::endl;
    const float xw = 0.5f, yw = 0.5f;
    BoxFilter f(xw, yw);

    Check(f.EvaluateX(0.f) == 1.f, "constant 1 at center");
    Check(f.EvaluateX(0.25f) == 1.f, "constant 1 inside footprint");
    Check(f.EvaluateX(xw) == 1.f, "windowed: 1 at the boundary");
    Check(f.EvaluateX(xw + 1e-3f) == 0.f, "zero beyond footprint");
    Check(f.EvaluateY(yw + 1e-3f) == 0.f, "zero beyond y footprint");
    CheckCommon(f, xw, yw);
}

void CheckTriangle() {
    std::cout << "-- triangle" << std::endl;
    const float xw = 2.f, yw = 2.f;
    TriangleFilter f(xw, yw);

    Check(std::abs(f.EvaluateX(0.f) - xw) < 1e-6f, "peak == xwidth at center");
    Check(f.EvaluateX(xw) == 0.f, "truncates at xwidth");
    Check(f.EvaluateX(xw + 0.5f) == 0.f, "zero beyond xwidth");
    CheckCommon(f, xw, yw);

    bool matches = true;
    for (float d = -3.f; d <= 3.f; d += 0.17f) {
        if (std::abs(f.EvaluateX(d) - RefTriangle(d, xw)) > 1e-6f)
            matches = false;
    }
    Check(matches, "matches scalar reference triangle");
}

void CheckCatmullRom() {
    std::cout << "-- catmullrom" << std::endl;
    const float xw = 4.f, yw = 4.f;
    CatmullRomFilter f(xw, yw);

    // Legacy kernel takes the raw offset with support |d| < 2.
    Check(std::abs(f.EvaluateX(0.f) - 2.f) < 1e-6f, "peak == 2 at center");
    Check(f.EvaluateX(2.f) == 0.f, "support ends at |d| == 2");
    Check(f.EvaluateX(3.f) == 0.f, "zero beyond support");
    CheckCommon(f, xw, yw);

    bool matches = true;
    for (float d = -3.f; d <= 3.f; d += 0.11f) {
        if (std::abs(f.EvaluateX(d) - RefCatmullRom(d)) > 1e-6f)
            matches = false;
    }
    Check(matches, "matches scalar reference catmullrom");
}

void CheckBlackmanHarris() {
    std::cout << "-- blackmanharris" << std::endl;
    const float xw = 4.f, yw = 4.f;
    BlackmanHarrisFilter f(xw, yw);

    const float peak = RefBlackmanHarris1D(0.f);
    Check(std::abs(f.EvaluateX(0.f) - peak) < 1e-6f, "peak matches window");
    Check(f.EvaluateX(xw) == 0.f, "truncates at xwidth");
    Check(f.EvaluateX(xw + 0.5f) == 0.f, "zero beyond xwidth");
    CheckCommon(f, xw, yw);

    bool matches = true;
    for (float d = -4.5f; d <= 4.5f; d += 0.13f) {
        const float ref = RefBlackmanHarris1D(d * (2.f / xw));
        if (std::abs(f.EvaluateX(d) - ref) > 1e-6f) matches = false;
    }
    Check(matches, "matches scalar reference blackmanharris");
}

void CheckSinc() {
    std::cout << "-- sinc" << std::endl;
    const float xw = 4.f, yw = 4.f, tau = 3.f;
    LanczosSincFilter f(xw, yw, tau);

    Check(f.EvaluateX(0.f) == 1.f, "unit value at center");
    Check(f.EvaluateX(xw + 0.5f) == 0.f, "zero beyond xwidth");
    CheckCommon(f, xw, yw);

    bool matches = true;
    for (float d = -4.5f; d <= 4.5f; d += 0.13f) {
        const float ref = RefSinc1D(d / xw, tau);
        if (std::abs(f.EvaluateX(d) - ref) > 1e-6f) matches = false;
    }
    Check(matches, "matches scalar reference lanczos-sinc");

    // The kernel must ring: at least one negative lobe inside the footprint.
    bool rings = false;
    for (float d = 0.f; d < xw * 0.99f; d += 0.05f)
        if (f.EvaluateX(d) < 0.f) rings = true;
    Check(rings, "has negative lobes (ringing kernel)");
}

void CheckMitchell() {
    std::cout << "-- mitchell" << std::endl;
    const float xw = 2.f, yw = 2.f, B = 1.f / 3.f, C = 1.f / 3.f;
    MitchellFilter f(xw, yw, B, C);

    Check(std::abs(f.GetB() - B) < 1e-6f, "GetB == B");
    Check(std::abs(f.GetC() - C) < 1e-6f, "GetC == C");
    Check(f.EvaluateX(xw) == 0.f, "truncates at xwidth");
    Check(f.EvaluateX(xw + 0.5f) == 0.f, "zero beyond xwidth");
    CheckCommon(f, xw, yw);

    bool matches = true;
    for (float d = -2.5f; d <= 2.5f; d += 0.11f) {
        const float ref = RefMitchell1D(std::abs(d) / xw, B, C);
        if (std::abs(f.EvaluateX(d) - ref) > 1e-6f) matches = false;
    }
    Check(matches, "matches scalar reference mitchell");

    // Cross-check against the Mitsuba 2 separable form: same cubic on the
    // normalized offset up to a constant positive scale, so the sign
    // structure must agree everywhere.
    bool signs = true;
    for (float d = 0.1f; d < xw; d += 0.05f) {
        const float a = f.EvaluateX(d);
        const float u = 2.f * d / xw;  // Mitsuba radius-2 parameter
        const float u2 = u * u, u3 = u2 * u;
        const float ms = (1.f / 6.f) *
            ((u < 1.f)
                 ? ((12.f - 9.f * B - 6.f * C) * u3 +
                    (-18.f + 12.f * B + 6.f * C) * u2 + (6.f - 2.f * B))
                 : ((-B - 6.f * C) * u3 + (6.f * B + 30.f * C) * u2 +
                    (-12.f * B - 48.f * C) * u + (8.f * B + 24.f * C)));
        if (std::abs(a) > 1e-6f && std::abs(ms) > 1e-6f &&
            (a > 0.f) != (ms > 0.f))
            signs = false;
    }
    Check(signs, "sign structure matches Mitsuba mitchell");
}

} // namespace

int main() {
    std::cout << "lux2 filter_check (Stage 3)" << std::endl;

    CheckGaussian();
    CheckBox();
    CheckTriangle();
    CheckCatmullRom();
    CheckBlackmanHarris();
    CheckSinc();
    CheckMitchell();

    std::cout << (g_failures == 0 ? "ALL PASSED" : "FAILURES")
              << " (" << g_failures << ")" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
