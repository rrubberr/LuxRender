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
#include "core/colorsystem.h"
#include "core/dynload.h"
#include "core/math.h"
#include "filters/gaussian.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <thread>
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

bool Close(float a, float b, float tol = 1e-4f) {
    return std::fabs(a - b) <= tol * (1.f + std::fabs(a) + std::fabs(b));
}

float RandUnit() { return float(std::rand()) / float(RAND_MAX); }

// Broadcast a scalar splat of a monochromatic spectrum value at wl.
void SplatOne(Film &film, float px, float py, float wl, float L,
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

// Compare all accumulation buffers of two films.
bool BuffersEqual(const FlexImageFilm &a, const FlexImageFilm &b,
                  float tol = 1e-4f) {
    const size_t n = a.BufX().size();
    if (n != b.BufX().size())
        return false;
    for (size_t i = 0; i < n; ++i) {
        if (!Close(a.BufX()[i], b.BufX()[i], tol) ||
            !Close(a.BufY()[i], b.BufY()[i], tol) ||
            !Close(a.BufZ()[i], b.BufZ()[i], tol) ||
            !Close(a.BufAlpha()[i], b.BufAlpha()[i], tol) ||
            !Close(a.BufWeight()[i], b.BufWeight()[i], tol))
            return false;
    }
    return true;
}

// A private block accumulates independently.
void CheckPrivateBlock() {
    GaussianFilter f(2.f, 2.f, 2.f);
    const float full[4] = {0.f, 1.f, 0.f, 1.f};
    FlexImageFilm master(64, 64, &f, full, "out", false);
    FlexImageFilm ref(64, 64, &f, full, "out", false);

    auto block = master.MakePrivateBlock(0, 0, 64, 64);
    Check(block != nullptr, "MakePrivateBlock returns a film");
    if (!block)
        return;

    // Geometry matches the master.
    Check(block->XRes() == master.XRes() && block->YRes() == master.YRes(),
          "block resolution matches master");

    FlexImageFilm *blk = dynamic_cast<FlexImageFilm *>(block.get());
    Check(blk != nullptr, "block is a FlexImageFilm");
    if (!blk)
        return;
    Check(blk->XStart() == master.XStart() && blk->XCount() == master.XCount() &&
              blk->YStart() == master.YStart() && blk->YCount() == master.YCount(),
          "block crop window matches master");

    // Splat into the block and the reference.
    const float wl = 560.f;
    SplatOne(*blk, 24.5f, 30.5f, wl, 1.3f, 1.f, 1.f);
    SplatOne(ref, 24.5f, 30.5f, wl, 1.3f, 1.f, 1.f);
    blk->AddSampleCount(3.0);
    ref.AddSampleCount(3.0);

    master.Merge(block.get());
    Check(BuffersEqual(master, ref), "block merge equals direct accumulation");
    Check(Close(float(master.SampleCount()), float(ref.SampleCount())),
          "block merge carries sample count");
}

// Clear() zeroes the buffers and sample count so a block can be reused.
void CheckClear() {
    GaussianFilter f(2.f, 2.f, 2.f);
    const float full[4] = {0.f, 1.f, 0.f, 1.f};
    FlexImageFilm film(64, 64, &f, full, "out", false);

    SplatOne(film, 32.5f, 32.5f, 550.f, 1.f, 1.f, 1.f);
    film.AddSampleCount(9.0);

    double before = 0.0;
    for (float w : film.BufWeight())
        before += w;
    Check(before > 0.0, "film has samples before clear");

    film.Clear();

    double after = 0.0;
    for (float w : film.BufWeight())
        after += w;
    Check(after == 0.0, "Clear zeroes weight buffer");
    Check(film.SampleCount() == 0.0, "Clear zeroes sample count");
}

// Concurrent merges from workers are serialized.
void CheckConcurrentMerge() {
    GaussianFilter f(2.f, 2.f, 2.f);
    const float full[4] = {0.f, 1.f, 0.f, 1.f};

    const int nWorkers = 8;
    const int splatsPerWorker = 40;
    const float wl = 590.f;

    // Build workers with deterministic splat sets.
    std::vector<std::unique_ptr<Film>> blocks;
    blocks.reserve(nWorkers);
    for (int w = 0; w < nWorkers; ++w) {
        auto b = FlexImageFilm(64, 64, &f, full, "out", false)
                     .MakePrivateBlock(0, 0, 64, 64);
        for (int i = 0; i < splatsPerWorker; ++i) {
            const float px = 8.f + float((w * 7 + i * 3) % 47);
            const float py = 8.f + float((w * 5 + i * 11) % 47);
            SplatOne(*b, px + 0.5f, py + 0.5f, wl, 1.f, 1.f, 1.f);
        }
        b->AddSampleCount(double(splatsPerWorker));
        blocks.push_back(std::move(b));
    }

    // Merge all blocks into one master sequentially.
    FlexImageFilm serialRef(64, 64, &f, full, "out", false);
    for (auto &b : blocks)
        serialRef.Merge(b.get());

    // Merge from nWorkers threads.
    FlexImageFilm master(64, 64, &f, full, "out", false);
    std::vector<std::thread> threads;
    threads.reserve(nWorkers);
    for (int w = 0; w < nWorkers; ++w) {
        threads.emplace_back([&master, &blocks, w]() {
            master.Merge(blocks[w].get());
        });
    }
    for (auto &t : threads)
        t.join();

    Check(BuffersEqual(master, serialRef),
          "concurrent merges equal serial merge");
    Check(Close(float(master.SampleCount()), float(serialRef.SampleCount())),
          "concurrent merges carry total sample count");
}

// A region block accumulates a sub-rect. MergeRegion maps it into the master.
void CheckRegionMerge() {
    GaussianFilter f(2.f, 2.f, 2.f);
    const float full[4] = {0.f, 1.f, 0.f, 1.f};
    const float wl = 560.f;

    FlexImageFilm master(64, 64, &f, full, "out", false);
    FlexImageFilm ref(64, 64, &f, full, "out", false);

    // Preexisting data outside the region must survive the merge.
    SplatOne(master, 4.5f, 4.5f, wl, 1.f, 1.f, 1.f);
    SplatOne(ref, 4.5f, 4.5f, wl, 1.f, 1.f, 1.f);

    // Owned region is [16,32) x [16,32).
    auto block = master.MakePrivateBlock(14, 14, 34, 34);
    Check(block != nullptr, "region MakePrivateBlock returns a film");
    if (!block)
        return;
    FlexImageFilm *blk = dynamic_cast<FlexImageFilm *>(block.get());
    Check(blk && blk->XStart() == 14 && blk->XCount() == 20 &&
              blk->YStart() == 14 && blk->YCount() == 20,
          "region block crop window is the requested rect");

    // Splat inside the region on both the block and the reference.
    for (int i = 0; i < 25; ++i) {
        const float px = 17.f + float(i % 5) + 0.5f;
        const float py = 17.f + float(i / 5) + 0.5f;
        SplatOne(*blk, px, py, wl, 1.f, 1.f, 1.f);
        SplatOne(ref, px, py, wl, 1.f, 1.f, 1.f);
    }
    blk->AddSampleCount(25.0);
    ref.AddSampleCount(25.0);

    master.MergeRegion(block.get(), 14, 14, 34, 34);

    Check(BuffersEqual(master, ref),
          "region merge equals direct accumulation (incl. out-of-region)");
    Check(Close(float(master.SampleCount()), float(ref.SampleCount())),
          "region merge carries sample count");

    bool zeroed = true;
    for (size_t i = 0; i < blk->BufWeight().size(); ++i)
        if (blk->BufWeight()[i] != 0.f || blk->BufX()[i] != 0.f ||
            blk->BufY()[i] != 0.f || blk->BufZ()[i] != 0.f ||
            blk->BufAlpha()[i] != 0.f)
            zeroed = false;
    Check(zeroed, "region merge zeroes the block for reuse");
    Check(blk->SampleCount() == 0.0, "region merge clears block count");
}

// Replicate the display pipeline from the film's raw accumulation buffers:
// normalize, apply the named tonemap kernel, convert to RGB via the film's
// colorspace, clamp, and gamma-encode. Returns the linear RGB and the 8-bit
// byte for one crop pixel; `meanY` is the autolinear scene average.
struct RefPixel {
    float lin[3];
    unsigned char byte[3];
};

RefPixel ExpectedPixel(const FlexImageFilm &film, int x, int y,
                       const char *kernel) {
    const int ix = x - film.XStart();
    const int iy = y - film.YStart();
    const size_t idx = size_t(iy) * film.XCount() + size_t(ix);
    const float w = film.BufWeight()[idx];

    RefPixel out{};
    if (w == 0.f)
        return out; // untouched pixel stays black

    XYZColor xyz(film.BufX()[idx] / w, film.BufY()[idx] / w,
                 film.BufZ()[idx] / w);

    const float gamma = film.LinearGamma();
    if (std::strcmp(kernel, "linear") == 0) {
        const float factor =
            film.LinearExposure() / (film.LinearFStop() * film.LinearFStop()) *
            film.LinearSensitivity() * 0.65f / 10.f *
            std::pow(118.f / 255.f, gamma);
        xyz *= factor;
    } else { // autolinear (EVOp)
        double sum = 0.0;
        int n = 0;
        for (size_t i = 0; i < film.BufWeight().size(); ++i) {
            const float ww = film.BufWeight()[i];
            if (ww == 0.f)
                continue;
            const float Y = film.BufY()[i] / ww;
            if (Y <= 0.f)
                continue;
            sum += Y;
            ++n;
        }
        const float meanY = n > 0 ? float(sum / n) : 0.f;
        if (meanY > 0.f)
            xyz *= (1.25f / meanY) * std::pow(118.f / 255.f, gamma);
    }

    const ColorSystem cs(film.ColorspaceRed()[0], film.ColorspaceRed()[1],
                         film.ColorspaceGreen()[0], film.ColorspaceGreen()[1],
                         film.ColorspaceBlue()[0], film.ColorspaceBlue()[1],
                         film.ColorspaceWhite()[0], film.ColorspaceWhite()[1],
                         1.f);
    const RGBColor limited = cs.Limit(cs.ToRGBConstrained(xyz), 0);
    for (int c = 0; c < 3; ++c)
        out.lin[c] = limited[c];

    const RGBColor g = limited.Pow(1.f / film.Gamma());
    for (int c = 0; c < 3; ++c)
        out.byte[c] = static_cast<unsigned char>(ClampF(255.f * g[c], 0.f, 255.f));
    return out;
}

void CheckFrameBufferBasics() {
    GaussianFilter f(2.f, 2.f, 2.f);
    const float full[4] = {0.f, 1.f, 0.f, 1.f};
    FlexImageFilm film(32, 32, &f, full, "out", false);

    // Lazily allocated buffers start all-zero.
    unsigned char *fb = film.GetFrameBuffer();
    float *ffb = film.GetFloatFrameBuffer();
    float *ab = film.GetAlphaBuffer();
    Check(fb != nullptr && ffb != nullptr && ab != nullptr,
          "framebuffer pointers non-null after lazy allocation");

    const size_t n = 32 * 32;
    bool zero = true;
    for (size_t i = 0; i < n * 3; ++i)
        if (fb[i] != 0 || ffb[i] != 0.f)
            zero = false;
    for (size_t i = 0; i < n; ++i)
        if (ab[i] != 0.f)
            zero = false;
    Check(zero, "framebuffer all-zero before any splat");
}

void CheckFrameBufferLinear() {
    GaussianFilter f(2.f, 2.f, 2.f);
    const float full[4] = {0.f, 1.f, 0.f, 1.f};
    FlexImageFilm film(32, 32, &f, full, "out", false);

    // Deterministic linear exposure: sensitivity=10, exposure=1, fstop=1.
    film.SetParameterValue(LUX_FILM_TM_TONEMAPKERNEL,
                           double(FlexImageFilm::TMK_LINEAR), 0);
    film.SetParameterValue(LUX_FILM_TM_LINEAR_SENSITIVITY, 10.0, 0);
    film.SetParameterValue(LUX_FILM_TM_LINEAR_EXPOSURE, 1.0, 0);
    film.SetParameterValue(LUX_FILM_TM_LINEAR_FSTOP, 1.0, 0);
    film.SetParameterValue(LUX_FILM_TM_LINEAR_GAMMA, 2.2, 0);

    // A uniform interior splat field so the center pixel is well-defined.
    // L is scaled so the tonemapped result lands mid-gray: saturation would
    // make the linear and gamma'd buffers indistinguishable at 1.0.
    const float wl = 550.f;
    for (int py = 6; py < 26; ++py)
        for (int px = 6; px < 26; ++px)
            SplatOne(film, float(px) + 0.5f, float(py) + 0.5f, wl, 2e-5f, 1.f, 1.f);

    film.UpdateFrameBuffer();
    unsigned char *fb = film.GetFrameBuffer();
    float *ffb = film.GetFloatFrameBuffer();

    const RefPixel ref = ExpectedPixel(film, 16, 16, "linear");
    const size_t off = size_t(16) * 32 + size_t(16);
    bool linOk = std::fabs(ffb[3 * off] - ref.lin[0]) < 1e-4f &&
                 std::fabs(ffb[3 * off + 1] - ref.lin[1]) < 1e-4f &&
                 std::fabs(ffb[3 * off + 2] - ref.lin[2]) < 1e-4f;
    Check(linOk, "float framebuffer matches linear reference (full-res offset)");

    bool byteOk = std::abs(int(fb[3 * off]) - int(ref.byte[0])) <= 1 &&
                  std::abs(int(fb[3 * off + 1]) - int(ref.byte[1])) <= 1 &&
                  std::abs(int(fb[3 * off + 2]) - int(ref.byte[2])) <= 1;
    Check(byteOk, "8-bit framebuffer matches gamma-encoded reference");

    // The float buffer is linear (pre-gamma); the byte buffer is gamma'd.
    // A monochromatic splat is saturated, so scan all three channels for one
    // strictly in range: there linear v and gamma'd v^(1/2.2) must differ,
    // proving gamma is applied exactly once and only to the 8-bit path.
    bool anyInRange = false, gammaDiffers = false;
    for (int c = 0; c < 3; ++c) {
        const float lin = ffb[3 * off + c];
        if (lin > 0.05f && lin < 0.95f) {
            anyInRange = true;
            if (std::fabs(lin - fb[3 * off + c] / 255.f) >= 1e-3f)
                gammaDiffers = true;
        }
    }
    Check(anyInRange, "tonemapped center pixel has an unsaturated channel");
    Check(gammaDiffers, "float buffer is linear while byte buffer is gamma'd");

    // A pixel with no contribution stays black in both buffers.
    const size_t corner = size_t(0) * 32 + size_t(0);
    Check(fb[3 * corner] == 0 && ffb[3 * corner] == 0.f,
          "uncovered pixel stays black at full-res offset");
}

void CheckFrameBufferAutoLinear() {
    GaussianFilter f(2.f, 2.f, 2.f);
    const float full[4] = {0.f, 1.f, 0.f, 1.f};
    FlexImageFilm film(32, 32, &f, full, "out", false);

    film.SetParameterValue(LUX_FILM_TM_TONEMAPKERNEL,
                           double(FlexImageFilm::TMK_AUTOLINEAR), 0);
    film.SetParameterValue(LUX_FILM_TM_LINEAR_GAMMA, 2.2, 0);

    const float wl = 500.f;
    for (int py = 6; py < 26; ++py)
        for (int px = 6; px < 26; ++px)
            SplatOne(film, float(px) + 0.5f, float(py) + 0.5f, wl, 0.3f, 1.f, 1.f);

    film.UpdateFrameBuffer();
    const RefPixel ref = ExpectedPixel(film, 16, 16, "autolinear");
    const size_t off = size_t(16) * 32 + size_t(16);
    float *ffb = film.GetFloatFrameBuffer();
    bool ok = std::fabs(ffb[3 * off] - ref.lin[0]) < 1e-4f &&
              std::fabs(ffb[3 * off + 1] - ref.lin[1]) < 1e-4f &&
              std::fabs(ffb[3 * off + 2] - ref.lin[2]) < 1e-4f;
    Check(ok, "float framebuffer matches autolinear reference");
}

void CheckFrameBufferCropOffset() {
    GaussianFilter f(2.f, 2.f, 2.f);
    // Crop the right-bottom quadrant of a 64x64 frame.
    const float crop[4] = {0.5f, 1.f, 0.5f, 1.f};
    FlexImageFilm film(64, 64, &f, crop, "out", false);
    Check(film.XStart() == 32 && film.YStart() == 32, "crop starts at (32,32)");

    film.SetParameterValue(LUX_FILM_TM_TONEMAPKERNEL,
                           double(FlexImageFilm::TMK_LINEAR), 0);
    film.SetParameterValue(LUX_FILM_TM_LINEAR_SENSITIVITY, 10.0, 0);
    film.SetParameterValue(LUX_FILM_TM_LINEAR_EXPOSURE, 1.0, 0);
    film.SetParameterValue(LUX_FILM_TM_LINEAR_FSTOP, 1.0, 0);

    const float wl = 550.f;
    for (int py = 36; py < 60; ++py)
        for (int px = 36; px < 60; ++px)
            SplatOne(film, float(px) + 0.5f, float(py) + 0.5f, wl, 0.05f, 1.f, 1.f);

    film.UpdateFrameBuffer();
    unsigned char *fb = film.GetFrameBuffer();

    // Inside the crop: written at full-res offset (48,48).
    const size_t inside = size_t(48) * 64 + size_t(48);
    Check(fb[3 * inside] != 0 || fb[3 * inside + 1] != 0 ||
              fb[3 * inside + 2] != 0,
          "crop pixel written at full-resolution offset");

    // Outside the crop (top-left): must remain untouched.
    const size_t outside = size_t(4) * 64 + size_t(4);
    Check(fb[3 * outside] == 0 && fb[3 * outside + 1] == 0 &&
              fb[3 * outside + 2] == 0,
          "pixels outside the crop window stay black");
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
    CheckPrivateBlock();
    CheckClear();
    CheckConcurrentMerge();
    CheckRegionMerge();
    CheckFrameBufferBasics();
    CheckFrameBufferLinear();
    CheckFrameBufferAutoLinear();
    CheckFrameBufferCropOffset();

    if (g_failures == 0) {
        std::cout << "lux2filmcheck: ALL CHECKS PASSED" << std::endl;
        return 0;
    }
    std::cerr << "lux2filmcheck: " << g_failures << " CHECK(S) FAILED"
              << std::endl;
    return 1;
}
