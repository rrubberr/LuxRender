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

// FlexImageFilm accumulation core.
// Checks registration, crop-window math, filter-weight conservation,
// normalized readback, Merge, validity rejection, and premultiplied alpha.

#include "film/fleximage.h"
#include "core/color.h"
#include "core/dynload.h"
#include "filters/gaussian.h"

#include <cmath>
#include <cstdlib>
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

bool Close(float a, float b, float tol = 1e-4f) {
    return std::fabs(a - b) <= tol * (1.f + std::fabs(a) + std::fabs(b));
}

float RandUnit() { return float(std::rand()) / float(RAND_MAX); }

// Broadcast a scalar splat of a monochromatic spectrum value at wl.
void SplatOne(FlexImageFilm &film, float px, float py, float wl, float L,
              float alpha, float weight) {
    SpectrumWavelengthsP sw;
    sw.FromWavelength(FloatP(wl));
    film.Splat(FloatP(px), FloatP(py), SWCSpectrumP(L), sw, FloatP(alpha),
               FloatP(weight), 0);
}

XYZColorP ExpectedXYZ(float wl, float L) {
    SpectrumWavelengthsP sw;
    sw.FromWavelength(FloatP(wl));
    return SWCToXYZ(SWCSpectrumP(L), sw);
}

float Lane0(const FloatP &p) {
    float buf[PACKET_WIDTH];
    enoki::store_unaligned(buf, p);
    return buf[0];
}

void CheckRegistration() {
    auto &reg = DynamicLoader::registeredFilms();
    Check(reg.count("fleximage") == 1, "fleximage registered");
    Check(reg.count("multiimage") == 1, "multiimage alias registered");
}

void CheckCropWindow() {
    GaussianFilter f(2.f, 2.f, 2.f);
    const float full[4] = {0.f, 1.f, 0.f, 1.f};
    FlexImageFilm a(800, 600, &f, full, "out", false);
    Check(a.XStart() == 0 && a.XCount() == 800 &&
          a.YStart() == 0 && a.YCount() == 600, "full crop");

    const float half[4] = {0.25f, 0.75f, 0.5f, 1.f};
    FlexImageFilm b(800, 600, &f, half, "out", false);
    Check(b.XStart() == 200 && b.XCount() == 400, "crop x window");
    Check(b.YStart() == 300 && b.YCount() == 300, "crop y window");
}

// Interior splats with weight 1: total accumulated weight equals splat count
// (each footprint is normalized to unit total).
void CheckWeightConservation() {
    GaussianFilter f(2.f, 2.f, 2.f);
    const float full[4] = {0.f, 1.f, 0.f, 1.f};
    FlexImageFilm film(64, 64, &f, full, "out", false);

    const float wl = 550.f;

    const int n = 100;
    for (int i = 0; i < n; ++i) {
        // Keep splat centers well inside so footprints are fully contained.
        const float px = 8.f + RandUnit() * 47.f;
        const float py = 8.f + RandUnit() * 47.f;
        SplatOne(film, px, py, wl, 1.f, 1.f, 1.f);
    }

    double total = 0.0;
    for (float w : film.BufWeight())
        total += w;

    // Each Splat call contributes one footprint per packet lane, each
    // normalized to unit total.
    const double expected = double(n) * double(PACKET_WIDTH);
    Check(std::fabs(total - expected) < 1e-3 * expected,
          "filter weight conservation (unit-normalized footprints)");
}

// A splat at a pixel center reads back (after normalization) as the expected
// XYZ scaled by the footprint center weight ratio.
void CheckSplatReadback() {
    GaussianFilter f(2.f, 2.f, 2.f);
    const float full[4] = {0.f, 1.f, 0.f, 1.f};
    FlexImageFilm film(64, 64, &f, full, "out", false);

    const float wl = 500.f;
    const float L = 0.8f;
    SplatOne(film, 32.5f, 32.5f, wl, L, 1.f, 1.f);

    const XYZColorP exp = ExpectedXYZ(wl, L);
    float xyz[3];
    float alpha;
    film.GetPixelNormalized(32, 32, xyz, &alpha);

    // Single splat: normalized L is the splat XYZ regardless of footprint.
    const bool ok = Close(xyz[0], Lane0(exp[0])) &&
                    Close(xyz[1], Lane0(exp[1])) &&
                    Close(xyz[2], Lane0(exp[2])) && Close(alpha, 1.f);
    if (!ok) {
        std::cerr << "  [dbg got=(" << xyz[0] << "," << xyz[1] << ","
                  << xyz[2] << ") exp=(" << Lane0(exp[0]) << ","
                  << Lane0(exp[1]) << "," << Lane0(exp[2]) << ")]\n";
    }
    Check(ok, "splat readback matches SWCToXYZ");

    // Neighbors must be non-zero (filter spreads) but unsampled far pixels
    // must be exactly zero.
    float nxyz[3], nalpha;
    film.GetPixelNormalized(33, 32, nxyz, &nalpha);
    Check(nxyz[1] > 0.f, "filter spreads to neighbor");
    film.GetPixelNormalized(60, 60, nxyz, &nalpha);
    Check(nxyz[0] == 0.f && nalpha == 0.f, "far pixel unsampled");
}

// Merging two films equals a film that received all splats directly.
void CheckMerge() {
    GaussianFilter f(2.f, 2.f, 2.f);
    const float full[4] = {0.f, 1.f, 0.f, 1.f};
    FlexImageFilm a(64, 64, &f, full, "out", false);
    FlexImageFilm b(64, 64, &f, full, "out", false);
    FlexImageFilm ref(64, 64, &f, full, "out", false);

    const float wl = 600.f;

    SplatOne(a, 20.5f, 20.5f, wl, 1.f, 1.f, 1.f);
    SplatOne(ref, 20.5f, 20.5f, wl, 1.f, 1.f, 1.f);
    a.AddSampleCount(7.0);
    ref.AddSampleCount(7.0);

    SplatOne(b, 30.5f, 25.5f, wl, 0.5f, 1.f, 2.f);
    SplatOne(ref, 30.5f, 25.5f, wl, 0.5f, 1.f, 2.f);
    b.AddSampleCount(5.0);
    ref.AddSampleCount(5.0);

    a.Merge(&b);

    bool ok = Close(float(a.SampleCount()), float(ref.SampleCount()));
    const size_t n = a.BufX().size();
    for (size_t i = 0; i < n && ok; ++i)
        ok = Close(a.BufX()[i], ref.BufX()[i]) &&
             Close(a.BufY()[i], ref.BufY()[i]) &&
             Close(a.BufZ()[i], ref.BufZ()[i]) &&
             Close(a.BufAlpha()[i], ref.BufAlpha()[i]) &&
             Close(a.BufWeight()[i], ref.BufWeight()[i]);
    Check(ok, "Merge equals direct accumulation");
}

// Invalid lanes (NaN, negative, infinite) are discarded like legacy.
void CheckValidity() {
    GaussianFilter f(2.f, 2.f, 2.f);
    const float full[4] = {0.f, 1.f, 0.f, 1.f};
    FlexImageFilm film(64, 64, &f, full, "out", false);

    const float wl = 550.f;

    // Negative weight: discarded.
    SplatOne(film, 32.5f, 32.5f, wl, 1.f, 1.f, -1.f);
    // NaN alpha: discarded.
    SplatOne(film, 32.5f, 32.5f, wl, 1.f, std::nanf(""), 1.f);

    double total = 0.0;
    for (float w : film.BufWeight())
        total += w;
    Check(total == 0.0, "invalid samples rejected");

    // Valid splat lands.
    SplatOne(film, 32.5f, 32.5f, wl, 1.f, 1.f, 1.f);
    total = 0.0;
    for (float w : film.BufWeight())
        total += w;
    Check(total > 0.0, "valid sample accepted");
}

// Premultiplied alpha scales the stored XYZ by alpha.
void CheckPremultiply() {
    GaussianFilter f(2.f, 2.f, 2.f);
    const float full[4] = {0.f, 1.f, 0.f, 1.f};
    FlexImageFilm pm(64, 64, &f, full, "out", true);
    FlexImageFilm np(64, 64, &f, full, "out", false);

    const float wl = 480.f;
    SplatOne(pm, 32.5f, 32.5f, wl, 0.9f, 0.5f, 1.f);
    SplatOne(np, 32.5f, 32.5f, wl, 0.9f, 0.5f, 1.f);

    float pxyz[3], nxyz[3], pa, na;
    pm.GetPixelNormalized(32, 32, pxyz, &pa);
    np.GetPixelNormalized(32, 32, nxyz, &na);

    Check(Close(pxyz[0], 0.5f * nxyz[0]) &&
          Close(pxyz[1], 0.5f * nxyz[1]) &&
          Close(pxyz[2], 0.5f * nxyz[2]), "premultiply scales XYZ by alpha");
    Check(Close(pa, na), "alpha buffer unaffected by premultiply");
}

// Live parameter get/set round-trips; unknown ids ignored.
void CheckParameters() {
    GaussianFilter f(2.f, 2.f, 2.f);
    const float full[4] = {0.f, 1.f, 0.f, 1.f};
    FlexImageFilm film(64, 64, &f, full, "out", false);

    Check(film.GetParameterValue(LUX_FILM_TM_TONEMAPKERNEL, 0) ==
              FlexImageFilm::TMK_AUTOLINEAR, "default kernel autolinear");
    film.SetParameterValue(LUX_FILM_TM_LINEAR_EXPOSURE, 4.0, 0);
    Check(Close(float(film.GetParameterValue(LUX_FILM_TM_LINEAR_EXPOSURE, 0)), 4.f),
          "exposure set/get");
    Check(Close(float(film.GetDefaultParameterValue(LUX_FILM_TM_LINEAR_EXPOSURE, 0)), 1.f),
          "default exposure unchanged");

    film.SetParameterValue(LUX_FILM_TORGB_X_RED, 0.7, 0);
    Check(Close(float(film.GetParameterValue(LUX_FILM_TORGB_X_RED, 0)), 0.7f),
          "colorspace red x set/get");
}

} // namespace

int main() {
    std::srand(20260925);
    std::cout << "lux2 film_check (Phase 6 Stage C)" << std::endl;

    CheckRegistration();
    CheckCropWindow();
    CheckWeightConservation();
    CheckSplatReadback();
    CheckMerge();
    CheckValidity();
    CheckPremultiply();
    CheckParameters();

    if (g_failures == 0) {
        std::cout << "lux2filmcheck: ALL CHECKS PASSED" << std::endl;
        return 0;
    }
    std::cerr << "lux2filmcheck: " << g_failures << " CHECK(S) FAILED"
              << std::endl;
    return 1;
}
