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

// Verifies the Queryable attribute model, the registry + XML dump, the
// %token% template formatter, RenderStatistics math, and the queryable C-API.

#include "core/api.h"
#include "core/context2.h"
#include "core/film.h"
#include "core/queryable.h"
#include "core/queryableregistry.h"
#include "core/renderstats.h"

#include <cmath>
#include <cstring>
#include <iostream>
#include <string>

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

bool RelClose(double a, double b, double tol) {
    return std::fabs(a - b) <= tol * (1.0 + std::fabs(a) + std::fabs(b));
}

// A Film whose sample count and halt parameters are set by the test.
class FakeFilm : public Film {
public:
    FakeFilm(int xres, int yres) : m_xres(xres), m_yres(yres) {}

    int XRes() const override { return m_xres; }
    int YRes() const override { return m_yres; }
    int HaltSpp() const override { return m_haltSpp; }
    int HaltTime() const override { return m_haltTime; }

    void Splat(const FloatP &, const FloatP &, const SWCSpectrumP &,
               const SpectrumWavelengthsP &, const FloatP &, const FloatP &,
               int) override {}
    void Merge(Film *) override {}
    void MergeRegion(Film *, int, int, int, int) override {}
    bool WriteImage(ImageType) override { return true; }

    void AddSampleCount(double n) override { m_sampleCount += n; }
    double SampleCount() const override { return m_sampleCount; }

    void SetSampleCount(double s) { m_sampleCount = s; }
    void SetHaltSpp(int h) { m_haltSpp = h; }
    void SetHaltTime(int h) { m_haltTime = h; }

private:
    int m_xres, m_yres;
    double m_sampleCount = 0.0;
    int m_haltSpp = 0;
    int m_haltTime = 0;
};

// A minimal Queryable to exercise the attribute model and registry directly.
class Probe : public Queryable {
public:
    Probe() : Queryable("probe") {
        AddIntAttribute(*this, "count", "an int", [this] { return m_count; },
                        [this](int v) { m_count = v; });
        AddDoubleAttribute(*this, "ratio", "a double", [this] { return m_ratio; });
        AddStringAttribute(*this, "label", "a string", [this] { return m_label; },
                           [this](const std::string &s) { m_label = s; });
        AddBoolAttribute(*this, "flag", "a bool", [this] { return m_flag; });
    }

    int m_count = 7;
    double m_ratio = 0.25;
    std::string m_label = "hello";
    bool m_flag = true;
};

void CheckAttributes() {
    Probe p;
    Check(p.HasAttribute("count"), "HasAttribute finds registered int");
    Check(!p.HasAttribute("missing"), "HasAttribute false for unknown");

    Check(p["count"].IntValue() == 7, "int getter returns value");
    Check(p["ratio"].DoubleValue() == 0.25, "double getter returns value");
    Check(p["label"].StringValue() == "hello", "string getter returns value");
    Check(p["flag"].BoolValue(), "bool getter returns value");

    Check(p["count"].Type() == AttributeType::Int, "int attribute type tag");
    Check(p["ratio"].TypeStr() == "double", "double TypeStr");
    Check(p["count"].Value() == "7", "int Value() string form");
    Check(p["flag"].Value() == "true", "bool Value() string form");

    // Cross-type numeric promotion.
    Check(p["count"].DoubleValue() == 7.0, "int promotes to double");
    Check(p["ratio"].FloatValue() == 0.25f, "double narrows to float");

    // Type mismatch throws.
    bool threw = false;
    try {
        (void)p["count"].BoolValue();
    } catch (...) {
        threw = true;
    }
    Check(threw, "int->bool getter throws");

    // Setters: writable vs read-only.
    p["count"].Set(42);
    Check(p.m_count == 42, "int setter mutates");
    p["label"].Set(std::string("world"));
    Check(p.m_label == "world", "string setter mutates");

    threw = false;
    try {
        p["ratio"].Set(1.0); // ratio registered read-only
    } catch (...) {
        threw = true;
    }
    Check(threw, "read-only setter throws");
}

void CheckTemplate() {
    Probe p;
    const std::string out =
        FormatTemplate(p, "%count%/%ratio% %label% 100%% done");
    Check(out == "7/0.25 hello 100% done",
          "FormatTemplate expands tokens and escapes %%");

    // Unknown token expands to empty.
    Check(FormatTemplate(p, "[%nope%]") == "[]",
          "FormatTemplate drops unknown tokens");
}

void CheckRegistry() {
    Probe p;
    QueryableRegistry reg;
    reg.Insert(&p);
    Check(reg.Has("probe"), "registry Has after Insert");
    Check(reg["probe"] == &p, "registry lookup returns object");
    Check(reg["absent"] == nullptr, "registry lookup null for absent");

    const char *xml = reg.GetContent();
    Check(xml != nullptr, "GetContent non-null");
    const std::string s(xml);
    Check(s.find("<name>probe</name>") != std::string::npos,
          "XML contains object name");
    Check(s.find("<name>count</name>") != std::string::npos,
          "XML contains attribute name");
    Check(s.find("<value>7</value>") != std::string::npos,
          "XML contains attribute value");
    Check(s.find("<type>double</type>") != std::string::npos,
          "XML contains attribute type");

    reg.Erase("probe");
    Check(!reg.Has("probe"), "registry Has false after Erase");
}

void CheckStatsMath() {
    // 100x50 crop = 5000 pixels.
    FakeFilm film(100, 50);
    film.SetSampleCount(20.0); // per-pixel spp
    film.SetHaltSpp(40);       // 50% complete

    RenderStatistics stats(film, /*targetSpp=*/40, /*threadCount=*/8);

    Check(RelClose(stats.PixelCount(), 5000.0, 1e-9), "PixelCount = XCount*YCount");
    Check(RelClose(stats.SamplesPerPixel(), 20.0, 1e-9),
          "SamplesPerPixel reads film per-pixel count");
    Check(RelClose(stats.PercentHaltSppComplete(), 50.0, 1e-6),
          "PercentHaltSppComplete = spp/haltspp*100");

    // haltTime unset -> percentHaltTime is 0, so percentComplete == spp branch.
    Check(RelClose(stats.PercentComplete(), 50.0, 1e-6),
          "PercentComplete = max(time,spp) with time unset");
    Check(std::isinf(stats.HaltTime()), "HaltTime infinity when unset");
    Check(!std::isinf(stats.HaltSpp()), "HaltSpp finite when set");

    // Remaining spp drives RemainingTime once a window rate exists.
    stats.Start();
    stats.UpdateWindow();
    film.SetSampleCount(24.0);
    stats.UpdateWindow();
    Check(stats.SamplesPerSecondWindow() >= 0.0,
          "window samples/sec non-negative");

    // Freeze the clock so ElapsedTime() is deterministic across reads, then
    // verify SamplesPerSecond = total samples / elapsed.
    stats.Stop();
    const double et = stats.ElapsedTime();
    if (et > 0.0) {
        const double expect = film.SampleCount() * stats.PixelCount() / et;
        Check(RelClose(stats.SamplesPerSecond(), expect, 1e-6),
              "SamplesPerSecond = total samples / elapsed");
    } else {
        Check(true, "SamplesPerSecond (elapsed==0 branch skipped)");
    }

    // Efficiency is the deferred seam: present but zero.
    Check(stats.Efficiency() == 0.0, "Efficiency returns 0 (deferred seam)");
}

void CheckFormatted() {
    FakeFilm film(64, 64);
    film.SetSampleCount(8.0);
    film.SetHaltSpp(16);
    RenderStatistics stats(film, 16, 4);

    const std::string rec =
        stats.formattedLong->GetRecommendedString();
    Check(!rec.empty(), "FormattedLong recommended string non-empty");
    // spp is 8 of 16 -> the percent token should show 50%.
    Check(rec.find("50%") != std::string::npos,
          "recommended string contains 50% halt-spp completion");
    Check(rec.find("S/p") != std::string::npos,
          "recommended string contains S/p unit");

    const std::string recShort =
        stats.formattedShort->GetRecommendedString();
    Check(!recShort.empty(), "FormattedShort recommended string non-empty");
}

void CheckCApi() {
    Context2 ctx;
    Context2::SetActive(&ctx);

    Probe p;
    ctx.Registry().Insert(&p);

    Check(luxHasObject("probe"), "luxHasObject sees registered object");
    Check(!luxHasObject("nope"), "luxHasObject false for absent");
    Check(luxHasAttribute("probe", "count"), "luxHasAttribute true");
    Check(luxGetAttributeType("probe", "ratio") == LUX_ATTRIBUTETYPE_DOUBLE,
          "luxGetAttributeType maps double");

    Check(luxGetIntAttribute("probe", "count") == 7, "luxGetIntAttribute value");
    Check(luxGetDoubleAttribute("probe", "ratio") == 0.25,
          "luxGetDoubleAttribute value");
    Check(luxGetBoolAttribute("probe", "flag"), "luxGetBoolAttribute value");

    char buf[64] = {0};
    luxGetStringAttribute("probe", "label", buf, sizeof(buf));
    Check(std::strcmp(buf, "hello") == 0, "luxGetStringAttribute copies value");

    // Setter round-trip through the C-API.
    luxSetIntAttribute("probe", "count", 99);
    Check(p.m_count == 99, "luxSetIntAttribute mutates");
    luxSetStringAttribute("probe", "label", "bye");
    Check(p.m_label == "bye", "luxSetStringAttribute mutates");

    // XML dump reachable through the C-API.
    const char *xml = luxGetAttributes();
    Check(xml && std::strstr(xml, "<name>probe</name>") != nullptr,
          "luxGetAttributes dumps registry");

    // No scene committed yet -> statistics report not-ready.
    Check(luxStatistics("sceneIsReady") == 0.0,
          "luxStatistics sceneIsReady false before commit");

    ctx.Registry().Erase("probe");
    Context2::SetActive(nullptr);
}

} // namespace

int main() {
    std::cout << "== Queryable attributes ==" << std::endl;
    CheckAttributes();
    std::cout << "== Template formatter ==" << std::endl;
    CheckTemplate();
    std::cout << "== Registry + XML ==" << std::endl;
    CheckRegistry();
    std::cout << "== RenderStatistics math ==" << std::endl;
    CheckStatsMath();
    std::cout << "== Formatted strings ==" << std::endl;
    CheckFormatted();
    std::cout << "== Queryable C-API ==" << std::endl;
    CheckCApi();

    if (g_failures == 0) {
        std::cout << "\nlux2statscheck: ALL CHECKS PASSED" << std::endl;
        return 0;
    }
    std::cerr << "\nlux2statscheck: " << g_failures << " CHECK(S) FAILED"
              << std::endl;
    return 1;
}
