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

// Stage 2: statistical validation of the per-packet path walk. Builds small
// scenes programmatically and checks a luminance-probe film's accumulated
// luminance against analytic expectations: black scene, and a direct emitter
// (maxdepth=0). Environment cases were dropped along with InfiniteLight.
// Also checks termination and finiteness.

#include "core/color.h"
#include "core/dynload.h"
#include "core/film.h"
#include "core/paramset.h"
#include "core/scene.h"
#include "core/spectrum.h"
#include "core/tilequeue.h"
#include "core/transform.h"
#include "integrators/path.h"

#include <cmath>
#include <iostream>
#include <memory>
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

bool Close(double a, double b, double tol) {
    const double d = std::fabs(a - b);
    const double m = std::max(1e-9, std::max(std::fabs(a), std::fabs(b)));
    return d <= tol * m;
}

// Numeric check that always prints actual vs. expected (and their ratio) so a
// failure can be diagnosed from the numbers rather than guessed at.
void CheckNum(const char *what, double actual, double expected, bool pass) {
    const double ratio = expected != 0.0 ? actual / expected
                                         : (actual == 0.0 ? 1.0 : 0.0);
    std::cout << (pass ? "  ok:   " : "  FAIL: ") << what
              << "  [actual=" << actual << " expected=" << expected
              << " ratio=" << ratio << "]" << std::endl;
    if (!pass)
        ++g_failures;
}

// Read lane 0 of a broadcast packet.
double First(const FloatP &p) {
    Float buf[PACKET_WIDTH];
    enoki::store_unaligned(buf, p);
    return double(buf[0]);
}

// A Film that keeps no pixels: folds each lane's spectral radiance into a
// luminance sum at its own wavelength. This is the former library NullFilm,
// dropped from the tree once FlexImageFilm covered production needs; the
// statistical checks below still want pixel-free luminance accounting.
class LuminanceProbe : public Film {
public:
    LuminanceProbe(int xres, int yres) : m_xres(xres), m_yres(yres) {}

    int XRes() const override { return m_xres; }
    int YRes() const override { return m_yres; }

    void Splat(const FloatP &, const FloatP &, const SWCSpectrumP &L,
               const SpectrumWavelengthsP &sw, const FloatP &alpha,
               const FloatP &weight, int) override {
        const FloatP lum = SWCY(L, sw) * weight;
        const MaskP active = alpha > FloatP(0.f);

        float lumArr[PACKET_WIDTH];
        uint32_t actArr[PACKET_WIDTH];
        enoki::store_unaligned(lumArr, lum);
        enoki::store_unaligned(actArr, active);

        for (size_t i = 0; i < PACKET_WIDTH; ++i) {
            if (actArr[i] != 0u && enoki::isfinite(lumArr[i])) {
                m_sumLuminance += double(lumArr[i]);
                m_count += 1.0;
            }
        }
    }

    void Merge(Film *other) override {
        LuminanceProbe *o = dynamic_cast<LuminanceProbe *>(other);
        if (!o)
            return;
        m_sumLuminance += o->m_sumLuminance;
        m_count += o->m_count;
        m_sampleCount += o->m_sampleCount;
    }

    bool WriteImage(ImageType) override { return true; }

    void AddSampleCount(double n) override { m_sampleCount += n; }
    double SampleCount() const override { return m_sampleCount; }

    double MeanLuminance() const {
        return m_count > 0.0 ? m_sumLuminance / m_count : 0.0;
    }

private:
    int m_xres, m_yres;
    double m_sumLuminance = 0.0;
    double m_count = 0.0;
    double m_sampleCount = 0.0;
};

// Mean luminance of a white (1,1,1) emitter of radiance `Le`, averaged over
// the wavelength range — the same normalization LuminanceProbe applies per
// lane.
double WhiteLeMeanLum(double Le) {
    double sum = 0.0;
    int n = 0;
    for (double wl = 380.0; wl < 720.0; wl += 1.0) {
        SpectrumWavelengthsP sw;
        sw.FromWavelength(FloatP(float(wl)));
        const RGBColorP white(FloatP(1.f), FloatP(1.f), FloatP(1.f));
        const SWCSpectrumP spd = RGBToSmitsSPD(white, sw, true);
        sum += First(SWCY(spd * FloatP(float(Le)), sw));
        ++n;
    }
    return sum / double(n);
}

// A committed scene: camera at `camPos` looking at `target`; a sphere of
// `radius` at the origin. If `emissive`, the sphere is a white area light of
// radiance `le`. `target` must differ from `camPos` or look_at degenerates
// (normalize(0)).
std::unique_ptr<Scene> MakeScene(const Point3f &camPos, const Point3f &target,
                                 float radius,
                                 const std::string &mat, bool emissive,
                                 double le, int spp) {
    SceneDescription d;
    const int xr = 16, yr = 16;
    d.filmParams.AddInt("xresolution", &xr, 1);
    d.filmParams.AddInt("yresolution", &yr, 1);
    d.samplerParams.AddInt("count", &spp, 1);

    d.cameraName = "perspective";
    d.cameraTransform =
        Transform::look_at(camPos, target, Vector3f(0.f, 1.f, 0.f));

    if (!mat.empty()) {
        ParamSet matte;
        const std::string mt = "matte";
        matte.AddString("type", &mt, 1);
        const RGBColor kd(float(mat == "matte1" ? 1.f : 0.5f),
                          float(mat == "matte1" ? 1.f : 0.5f),
                          float(mat == "matte1" ? 1.f : 0.5f));
        matte.AddRGBColor("Kd", &kd, 1);
        d.namedMaterials["m"] = matte;
    }

    ShapeDesc sd;
    sd.name = "sphere";
    sd.toWorld = Transform();
    float r = radius;
    sd.params.AddFloat("radius", &r, 1);
    if (!mat.empty()) {
        sd.material.isNamed = true;
        sd.material.namedRef = "m";
    }
    if (emissive) {
        sd.isAreaLight = true;
        sd.areaLightName = "area";
        const RGBColor leRGB = RGBColor(Float(le), Float(le), Float(le));
        // The area-light plugin reads the radiance parameter as "L"; a
        // misspelled key falls back silently to RGBColor(1.f).
        sd.areaLightParams.AddRGBColor("L", &leRGB, 1);
    }
    d.shapes.push_back(sd);

    auto scene = std::make_unique<Scene>();
    scene->Commit(d);
    return scene;
}

// Render one frame tile into a fresh luminance probe.
double RunPass(SurfaceIntegrator &integ, Scene &scene) {
    integ.Start(scene);
    Tile t;
    t.x0 = 0; t.y0 = 0; t.x1 = 16; t.y1 = 16;
    LuminanceProbe probe(16, 16);
    auto sampler = scene.GetSampler().Clone();
    integ.RenderTile(scene, t, probe, *sampler, 0, sampler->SampleCount());
    return probe.MeanLuminance();
}

// Two emissive spheres far apart (no mutual visibility), each in its own named
// light group. The camera frames both. Used to check per-group filtering: a
// pass with group g active sees only emitter g. With singleGroup, both
// emitters share group 0 so one render is the combined reference (the
// sequential design has no all-groups pass).
std::unique_ptr<Scene> MakeTwoGroupScene(double leA, double leB, int spp,
                                         bool singleGroup = false) {
    SceneDescription d;
    const int xr = 16, yr = 16;
    d.filmParams.AddInt("xresolution", &xr, 1);
    d.filmParams.AddInt("yresolution", &yr, 1);
    d.samplerParams.AddInt("count", &spp, 1);

    d.cameraName = "perspective";
    d.cameraTransform =
        Transform::look_at(Point3f(0.f, 0.f, 8.f), Point3f(0.f, 0.f, 0.f),
                           Vector3f(0.f, 1.f, 0.f));

    // Groups are normally resolved at parse time (Context2::ResolveLightGroup).
    // This test builds the description directly, so assign indices and the
    // ordered name list by hand: group 0 = "A", group 1 = "B".
    auto emitter = [&](float x, double radiance, std::uint32_t groupIndex) {
        ShapeDesc sd;
        sd.name = "sphere";
        sd.toWorld = Transform::translate(Vector3f(x, 0.f, 0.f));
        float r = 1.f;
        sd.params.AddFloat("radius", &r, 1);
        sd.isAreaLight = true;
        sd.areaLightName = "area";
        const RGBColor leRGB =
            RGBColor(Float(radiance), Float(radiance), Float(radiance));
        // Radiance parameter key is "L" (see MakeScene); "Le" is ignored.
        sd.areaLightParams.AddRGBColor("L", &leRGB, 1);
        sd.lightGroupIndex = groupIndex;
        d.shapes.push_back(sd);
    };
    emitter(-3.f, leA, 0u);
    emitter(3.f, leB, singleGroup ? 0u : 1u);
    d.lightGroups = singleGroup ? std::vector<std::string>{"A"}
                                : std::vector<std::string>{"A", "B"};

    auto scene = std::make_unique<Scene>();
    scene->Commit(d);
    return scene;
}

std::shared_ptr<SurfaceIntegrator> MakeIntegrator(int maxdepth, bool nee,
                                                  bool includeEnv) {
    auto &siReg = DynamicLoader::registeredSurfaceIntegrators();
    auto it = siReg.find("path");
    if (it == siReg.end())
        return nullptr;
    ParamSet params;
    params.AddInt("maxdepth", &maxdepth, 1);
    params.AddBool("directlightsampling", &nee, 1);
    params.AddBool("includeenvironment", &includeEnv, 1);
    PluginContext pctx;
    pctx.params = &params;
    return it->second(pctx);
}

} // namespace

int main() {
    std::cout << "lux2 path_check (Stage 2)" << std::endl;

    // ---- 1. registration + construction ------------------------------
    {
        auto &siReg = DynamicLoader::registeredSurfaceIntegrators();
        Check(siReg.count("path") == 1, "PathIntegrator registered as 'path'");
        auto integ = MakeIntegrator(6, true, true);
        Check(integ != nullptr, "PathIntegrator constructed via registry");
        if (integ) {
            Check(integ->PassCount() == 1, "PassCount() == 1");
            auto *path = dynamic_cast<PathIntegrator *>(integ.get());
            Check(path && path->MaxDepth() == 6, "maxdepth param parsed (6)");
        }
    }

    // ---- 2. black scene: no lights -> L == 0 --------------------------
    {
        auto scene = MakeScene(Point3f(0.f, 0.f, 4.f), Point3f(0.f, 0.f, 0.f),
                               1.f, "matte",
                               false, 0.0, 64);
        auto integ = MakeIntegrator(6, true, true);
        const double mean = RunPass(*integ, *scene);
        Check(std::isfinite(mean), "black scene: finite");
        Check(mean < 1e-6, "black scene: L == 0");
    }

    // ---- 3. direct emitter visible (maxdepth=0) -> mean == Le ---------
    {
        const double Le = 1.0;
        auto scene = MakeScene(Point3f(0.f, 0.f, 4.f), Point3f(0.f, 0.f, 0.f),
                               1.f, "",
                               true, Le, 64);
        auto integ = MakeIntegrator(0, true, true);
        const double mean = RunPass(*integ, *scene);
        const double expected = WhiteLeMeanLum(Le);
        Check(std::isfinite(mean), "direct emitter: finite");
        CheckNum("direct emitter: mean == Le", mean, expected,
                 Close(mean, expected, 0.15));
    }

    // ---- 3b. radiance scaling guard ------------------------------------
    // Section 3 uses Le == 1, which coincides with the area-light default, so
    // a misspelled/ignored radiance parameter would pass it by accident. This
    // variant uses Le == 2: if the parameter is dead the mean stays at the
    // Le == 1 level and this check fails, catching the silent fallback.
    {
        auto scene = MakeScene(Point3f(0.f, 0.f, 4.f), Point3f(0.f, 0.f, 0.f),
                               1.f, "",
                               true, 2.0, 64);
        auto integ = MakeIntegrator(0, true, true);
        const double mean2 = RunPass(*integ, *scene);
        const double expected2 = WhiteLeMeanLum(2.0);
        CheckNum("direct emitter: Le=2 doubles the radiance", mean2, expected2,
                 Close(mean2, expected2, 0.15));
    }

    // ---- 4. environment cases: DROPPED --------------------------------
    // The uniform-environment NEE and enclosed-shell cases required
    // InfiniteLight, which is out of the tree pending its return.

    // ---- 5. furnace: DEFERRED ------------------------------------------
    // A closed emissive cavity with albedo 1 has no equilibrium: L diverges
    // as the geometric series Le/(1-rho), so the integrator's maxdepth-
    // truncated value grows with depth rather than converging to Le. A correct
    // furnace test needs rho < 1 (expect Le/(1-rho)) or a blackbody (rho == 0,
    // expect Le). Removed pending a properly-posed cavity case.

    // ---- 6. per-group light filtering (Stage 4) ------------------------
    // Two emitters in groups A and B. With group g active the pass sees only
    // emitter g; the sum of the two group passes matches the combined render
    // within statistical tolerance (guards the MIS restricted-pdf invariant).
    {
        const double LeA = 1.0, LeB = 2.0;
        const int spp = 512;

        auto scene = MakeTwoGroupScene(LeA, LeB, spp);
        Check(scene->LightGroupCount() == 2, "groups: two groups committed");
        Check(scene->AreaLightGroup(0) == 0 && scene->AreaLightGroup(1) == 1,
              "groups: area lights in distinct groups");

        auto integ = MakeIntegrator(0, true, true); // direct emitter only

        // Each emitter covers roughly half the frame, so a group pass sees
        // about half its own Le. The exact coverage fraction is not asserted;
        // what matters is that each pass isolates its emitter and the passes
        // sum to the combined render (the MIS restricted-pdf invariant).
        const double expA = 0.5 * WhiteLeMeanLum(LeA);
        const double expB = 0.5 * WhiteLeMeanLum(LeB);

        // Group A active -> only emitter A (Le=1) contributes.
        scene->SetActiveGroup(0);
        const double meanA = RunPass(*integ, *scene);
        Check(meanA > 1e-4, "group A pass: nonzero (emitter A visible)");
        CheckNum("group A pass ~= half Le_A", meanA, expA,
                 Close(meanA, expA, 0.2));

        // Group B active -> only emitter B (Le=2).
        scene->SetActiveGroup(1);
        const double meanB = RunPass(*integ, *scene);
        CheckNum("group B pass ~= half Le_B", meanB, expB,
                 Close(meanB, expB, 0.2));

        // Group B's emitter is twice as bright, so its pass must exceed A's.
        Check(meanB > 1.5 * meanA, "group B pass exceeds group A (Le ratio)");

        // Combined reference: the same two emitters placed in a single group,
        // so one render sees both. The sequential design has no all-groups
        // pass; the sum of the per-group passes matches this combined render
        // within statistical tolerance (the MIS restricted-pdf invariant).
        auto combined = MakeTwoGroupScene(LeA, LeB, spp, /*singleGroup=*/true);
        const double meanAll = RunPass(*integ, *combined);
        Check(meanAll > meanA && meanAll > meanB,
              "combined render exceeds either single group");
        CheckNum("sum of group passes ~= combined", meanA + meanB, meanAll,
                 Close(meanA + meanB, meanAll, 0.1));
    }

    std::cout << (g_failures == 0 ? "ALL PASS" : "FAILURES")
              << " (" << g_failures << ")" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
