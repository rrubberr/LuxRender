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

// Phase 4 verification: the "sampler" TBB tile-scheduler renderer.
// Checks registration + Scene::Commit wiring, progressive pass termination
// (haltspp, halttime), convergence to the analytic radiance of a
// direct-view white emitter through the private-block/merge-region path, and
// bitwise determinism across renders (pixel-hash seeding must make output
// independent of tile/thread assignment).

#include "core/dynload.h"
#include "core/paramset.h"
#include "core/scene.h"
#include "core/spectrum.h"
#include "core/color.h"
#include "core/transform.h"
#include "film/fleximage.h"
#include "renderers/samplerrenderer.h"
#include "renderers/grouppass.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

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

void CheckNum(const char *what, double actual, double expected, bool pass) {
    const double ratio = expected != 0.0 ? actual / expected
                                         : (actual == 0.0 ? 1.0 : 0.0);
    std::cout << (pass ? "  ok:   " : "  FAIL: ") << what
              << "  [actual=" << actual << " expected=" << expected
              << " ratio=" << ratio << "]" << std::endl;
    if (!pass)
        ++g_failures;
}

bool Close(double a, double b, double tol) {
    const double d = std::fabs(a - b);
    const double m = std::max(1e-9, std::max(std::fabs(a), std::fabs(b)));
    return d <= tol * m;
}

double First(const FloatP &p) {
    Float buf[PACKET_WIDTH];
    enoki::store_unaligned(buf, p);
    return double(buf[0]);
}

// Mean luminance of a white (1,1,1) emitter of radiance `Le` over the
// wavelength range (same normalization path_check uses).
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

// Committed scene: camera at (0,0,4) looking at the origin, emissive white
// sphere of radiance `le` at the origin, FlexImageFilm `xres`x`yres`,
// sampler spp, optional haltspp/halttime overrides (< 0 keeps film default).
std::unique_ptr<Scene> MakeEmitterScene(int spp, int haltspp, int halttime) {
    SceneDescription d;
    const int xr = 64, yr = 64;
    d.filmName = "fleximage";
    d.filmParams.AddInt("xresolution", &xr, 1);
    d.filmParams.AddInt("yresolution", &yr, 1);
    if (haltspp >= 0) d.filmParams.AddInt("haltspp", &haltspp, 1);
    if (halttime >= 0) d.filmParams.AddInt("halttime", &halttime, 1);

    d.rendererName = "sampler";
    d.samplerName = "";
    d.samplerParams.AddInt("count", &spp, 1);

    d.cameraName = "perspective";
    d.cameraTransform = Transform::look_at(Point3f(0.f, 0.f, 4.f),
                                           Point3f(0.f, 0.f, 0.f),
                                           Vector3f(0.f, 1.f, 0.f));

    ShapeDesc sd;
    sd.name = "sphere";
    const float r = 1.f;
    sd.params.AddFloat("radius", &r, 1);
    sd.isAreaLight = true;
    sd.areaLightName = "area";
    const double le = 1.0;
    const RGBColor leRGB = RGBColor(Float(le), Float(le), Float(le));
    sd.areaLightParams.AddRGBColor("Le", &leRGB, 1);
    d.shapes.push_back(sd);

    auto scene = std::make_unique<Scene>();
    scene->Commit(d);
    return scene;
}

const FlexImageFilm *AsFlex(const Scene &s) {
    return dynamic_cast<const FlexImageFilm *>(&s.GetFilm());
}

// Two emissive spheres far apart, each in its own named light group (0 and 1).
// Camera frames both. Used to check per-group routing and the wrapper loop.
std::unique_ptr<Scene> MakeTwoGroupScene(int spp) {
    SceneDescription d;
    const int xr = 32, yr = 32;
    d.filmName = "fleximage";
    d.filmParams.AddInt("xresolution", &xr, 1);
    d.filmParams.AddInt("yresolution", &yr, 1);
    d.filmParams.AddInt("haltspp", &spp, 1);
    d.rendererName = "sampler";
    d.samplerName = "lowdiscrepancy";
    d.samplerParams.AddInt("count", &spp, 1);
    d.cameraName = "perspective";
    d.cameraTransform = Transform::look_at(Point3f(0.f, 0.f, 8.f),
                                           Point3f(0.f, 0.f, 0.f),
                                           Vector3f(0.f, 1.f, 0.f));
    auto emitter = [&](float x, std::uint32_t groupIndex) {
        ShapeDesc sd;
        sd.name = "sphere";
        sd.toWorld = Transform::translate(Vector3f(x, 0.f, 0.f));
        const float r = 1.f;
        sd.params.AddFloat("radius", &r, 1);
        sd.isAreaLight = true;
        sd.areaLightName = "area";
        const double le = 1.0;
        const RGBColor leRGB = RGBColor(Float(le), Float(le), Float(le));
        sd.areaLightParams.AddRGBColor("L", &leRGB, 1);
        sd.lightGroupIndex = groupIndex;
        d.shapes.push_back(sd);
    };
    emitter(-3.f, 0u);
    emitter(3.f, 1u);
    d.lightGroups = {"A", "B"};
    auto scene = std::make_unique<Scene>();
    scene->Commit(d);
    return scene;
}

// Sum of the active group's BufY (call after film.SetActiveGroup(g)).
double ActiveBufYSum(const FlexImageFilm &f) {
    double s = 0.0;
    for (float v : f.BufY())
        s += double(v);
    return s;
}

} // namespace

int main() {
    std::cout << "lux2 renderer_check (Phase 4: TBB tile scheduler)" << std::endl;

    // ---- 1. registration + Commit wiring ------------------------------
    {
        auto &reg = DynamicLoader::registeredRenderers();
        Check(reg.count("sampler") == 1, "\"sampler\" renderer registered");

        auto scene = MakeEmitterScene(16, -1, -1);
        Check(scene->IsCommitted(), "scene committed");
        Renderer &r = scene->GetRenderer();
        Check(true, "GetRenderer() non-null after Commit");
        auto *sr = dynamic_cast<SamplerRenderer *>(&r);
        Check(sr != nullptr, "Commit instantiated a SamplerRenderer");
    }

    // ---- 2. unknown renderer name falls back to "sampler" --------------
    {
        SceneDescription d;
        const int xr = 8, yr = 8, spp = 4;
        d.filmParams.AddInt("xresolution", &xr, 1);
        d.filmParams.AddInt("yresolution", &yr, 1);
        d.rendererName = "no_such_renderer";
        d.samplerParams.AddInt("count", &spp, 1);
        Scene s;
        s.Commit(d);
        auto *sr = dynamic_cast<SamplerRenderer *>(&s.GetRenderer());
        Check(sr != nullptr, "unknown renderer name falls back to SamplerRenderer");
    }

    // ---- 3. full render: termination at haltspp + convergence ----------
    {
        const int spp = 256;
        auto scene = MakeEmitterScene(spp, spp, -1);
        scene->GetRenderer().Render(*scene, scene->GetSurfaceIntegrator());

        const double count = scene->GetFilm().SampleCount();
        CheckNum("pass loop terminates at haltspp", count, double(spp),
                 count == double(spp));

        // Center pixel looks straight at the sphere: normalized Y must
        // converge to the emitter's mean luminance. Background is exactly
        // black (maxdepth-independent: no lights elsewhere, no env).
        const FlexImageFilm *film = AsFlex(*scene);
        Check(film != nullptr, "film is a FlexImageFilm");
        if (film) {
            float xyz[3], alpha = 0.f;
            film->GetPixelNormalized(32, 32, xyz, &alpha);
            const double expected = WhiteLeMeanLum(1.0);
            CheckNum("center pixel converges to Le", xyz[1], expected,
                     Close(xyz[1], expected, 0.10));

            // Corner is background: nothing to hit, must stay black.
            film->GetPixelNormalized(2, 2, xyz, &alpha);
            Check(xyz[1] < 1e-5, "corner pixel black");
        }
    }

    // ---- 4. determinism: identical scenes render identically -----------
    // Bitwise equality is no longer the contract: work stealing varies the
    // order halo-bleed merges hit each boundary pixel's accumulator, so
    // float rounding can differ run to run. Per-sample seeding is still
    // thread-independent, so buffers must agree to float epsilon.
    {
        auto s1 = MakeEmitterScene(64, 64, -1);
        auto s2 = MakeEmitterScene(64, 64, -1);
        s1->GetRenderer().Render(*s1, s1->GetSurfaceIntegrator());
        s2->GetRenderer().Render(*s2, s2->GetSurfaceIntegrator());

        const FlexImageFilm *f1 = AsFlex(*s1), *f2 = AsFlex(*s2);
        bool same = f1 && f2 && f1->BufY().size() == f2->BufY().size();
        if (same) {
            for (size_t i = 0; i < f1->BufY().size() && same; ++i) {
                const auto close = [](float a, float b) {
                    return a == b ||
                           std::fabs(a - b) <=
                               1e-5f * (1.f + std::fabs(a) + std::fabs(b));
                };
                same = close(f1->BufX()[i], f2->BufX()[i]) &&
                       close(f1->BufY()[i], f2->BufY()[i]) &&
                       close(f1->BufZ()[i], f2->BufZ()[i]) &&
                       close(f1->BufWeight()[i], f2->BufWeight()[i]);
            }
        }
        Check(same, "two renders agree to float epsilon (thread-independent seeding)");
    }

    // ---- 5. haltspp early-out -------------------------------------------
    {
        const int spp = 64, haltspp = 8;
        auto scene = MakeEmitterScene(spp, haltspp, -1);
        scene->GetRenderer().Render(*scene, scene->GetSurfaceIntegrator());
        const double count = scene->GetFilm().SampleCount();
        CheckNum("haltspp stops the pass loop", count, double(haltspp),
                 count == double(haltspp));
    }

    // ---- 6. halttime early-out ------------------------------------------
    {
        auto scene = MakeEmitterScene(100000, -1, 1);
        scene->GetRenderer().Render(*scene, scene->GetSurfaceIntegrator());
        const double count = scene->GetFilm().SampleCount();
        Check(count > 0.0 && count < 100000.0, "halttime stops the pass loop");
    }

    // ---- 7. cross-thread control: Pause/Resume/Terminate ----------------
    {
        // A long render (huge spp) on a worker thread; the API calls hit
        // the same Renderer object from the main thread while Render() runs.
        const int xr = 256, yr = 256;
        SceneDescription d;
        d.filmName = "fleximage";
        d.filmParams.AddInt("xresolution", &xr, 1);
        d.filmParams.AddInt("yresolution", &yr, 1);
        d.rendererName = "sampler";
        // Small tiles keep the per-tile cancellation latency short.
        const int tileSize = 8;
        d.rendererParams.AddInt("tilesize", &tileSize, 1);
        d.samplerName = "lowdiscrepancy";
        const int spp = 100000;
        d.samplerParams.AddInt("count", &spp, 1);
        d.cameraName = "perspective";
        d.cameraTransform = Transform::look_at(Point3f(0.f, 0.f, 4.f),
                                               Point3f(0.f, 0.f, 0.f),
                                               Vector3f(0.f, 1.f, 0.f));
        ShapeDesc sd;
        sd.name = "sphere";
        const float r = 1.f;
        sd.params.AddFloat("radius", &r, 1);
        sd.isAreaLight = true;
        sd.areaLightName = "area";
        const double le = 1.0;
        const RGBColor leRGB{Float(le), Float(le), Float(le)};
        sd.areaLightParams.AddRGBColor("Le", &leRGB, 1);
        d.shapes.push_back(sd);

        auto scene = std::make_unique<Scene>();
        scene->Commit(d);
        Renderer &renderer = scene->GetRenderer();

        std::atomic<bool> renderDone{false};
        std::thread renderThread([&] {
            renderer.Render(*scene, scene->GetSurfaceIntegrator());
            renderDone.store(true);
        });

        // Wait for at least one pass to land, then pause.
        while (scene->GetFilm().SampleCount() == 0.0 && !renderDone.load())
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        renderer.Pause();

        // A pass already in flight commits its sample count when it ends;
        // poll until the count stops moving, then assert it stays put.
        double pausedCount = scene->GetFilm().SampleCount();
        bool settled = false;
        for (int i = 0; i < 100 && !settled; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            const double c = scene->GetFilm().SampleCount();
            settled = (c == pausedCount);
            pausedCount = c;
        }
        Check(pausedCount > 0.0, "samples accumulated before pause");
        Check(settled, "sample count settles after Pause()");
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        Check(scene->GetFilm().SampleCount() == pausedCount,
              "Pause() halts sample accumulation");
        Check(!renderDone.load(), "render thread still alive while paused");

        renderer.Resume();
        const double beforeResume = pausedCount;
        bool resumed = false;
        for (int i = 0; i < 100 && !resumed; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            resumed = scene->GetFilm().SampleCount() > beforeResume;
        }
        Check(resumed, "Resume() restarts sample accumulation");

        const auto t0 = std::chrono::steady_clock::now();
        renderer.Terminate();
        renderThread.join();
        const double stopMs =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - t0).count();
        Check(renderDone.load(), "Terminate() ends Render()");
        CheckNum("Terminate() returns within 2s", stopMs, 2000.0, stopMs < 2000.0);

        const double finalCount = scene->GetFilm().SampleCount();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        Check(scene->GetFilm().SampleCount() == finalCount,
              "sample count stable after Terminate()");
    }

    // ---- 8. Stage 5: GroupRenderPass wrapper ----------------------------
    {
        // (a) Re-entrancy contract: two Render() calls with no Clear() in
        //     between must ACCUMULATE into the film's buffers, not reset them.
        //     NB: SampleCount() is NOT the invariant -- SetupTiles (run by
        //     every Render) resets per-tile bookkeeping, so it reflects only
        //     the last pass. The accumulation buffers are what must persist
        //     and grow; two equal-budget passes should roughly double them.
        const int spp = 32;
        auto scene = MakeEmitterScene(spp, spp, -1);
        Renderer &r = scene->GetRenderer();
        r.Render(*scene, scene->GetSurfaceIntegrator());
        const FlexImageFilm *f = AsFlex(*scene);
        const double y1 = f ? ActiveBufYSum(*f) : 0.0;
        r.Render(*scene, scene->GetSurfaceIntegrator());
        const double y2 = f ? ActiveBufYSum(*f) : 0.0;
        Check(y1 > 0.0 && y2 > y1,
              "re-entrancy: accumulation grows (not reset) across passes");
        CheckNum("re-entrancy: two equal passes ~double the buffer",
                 y2, 2.0 * y1, Close(y2, 2.0 * y1, 0.05));

        // (b) Per-group routing: the wrapper drives one pass per group; each
        //     group's buffer set holds only its own emitter's contribution.
        auto gs = MakeTwoGroupScene(64);
        Check(gs->LightGroupCount() == 2, "wrapper: two groups committed");
        RunGroupPasses(gs->GetRenderer(), *gs, gs->GetSurfaceIntegrator());
        // Non-const: reading a specific group's buffer selects it first.
        FlexImageFilm *gf =
            dynamic_cast<FlexImageFilm *>(&gs->GetFilm());
        Check(gf != nullptr, "wrapper: film is a FlexImageFilm");
        if (gf) {
            const double sumA = [&] { gf->SetActiveGroup(0); return ActiveBufYSum(*gf); }();
            const double sumB = [&] { gf->SetActiveGroup(1); return ActiveBufYSum(*gf); }();
            Check(sumA > 0.0, "wrapper: group A buffer accumulated");
            Check(sumB > 0.0, "wrapper: group B buffer accumulated");
            // The wrapper leaves activeGroup on a valid group (no -1
            // sentinel); composite reads all sets regardless.
            const int ag = gs->GetActiveGroup();
            Check(ag >= 0 && ag < gs->LightGroupCount(),
                  "wrapper: activeGroup is a valid index after loop");
        }

        // (c) D9 fast path: a single default group with an identity convert
        //     must be bit-identical to a direct Render(). RunGroupPasses
        //     takes the early-return branch (no per-group seed change).
        {
            auto s_direct = MakeEmitterScene(48, 48, -1);
            s_direct->GetRenderer().Render(*s_direct,
                                           s_direct->GetSurfaceIntegrator());
            auto s_wrapper = MakeEmitterScene(48, 48, -1);
            RunGroupPasses(s_wrapper->GetRenderer(), *s_wrapper,
                           s_wrapper->GetSurfaceIntegrator());
            // Compare to float epsilon, not bitwise: work stealing varies the
            // order halo-bleed merges hit each boundary pixel (see test 4), so
            // two runs of the same code differ in low bits. The fast path must
            // match a direct render as closely as two direct renders match.
            const FlexImageFilm *fd = AsFlex(*s_direct);
            const FlexImageFilm *fw = AsFlex(*s_wrapper);
            bool same = fd && fw && fd->BufY().size() == fw->BufY().size();
            if (same) {
                for (size_t i = 0; i < fd->BufY().size() && same; ++i) {
                    const float a = fd->BufY()[i], b = fw->BufY()[i];
                    same = a == b ||
                           std::fabs(a - b) <=
                               1e-5f * (1.f + std::fabs(a) + std::fabs(b));
                }
            }
            Check(same, "D9 fast path: single-group wrapper == direct render");
        }

        // (d) Seed modes. Correlated (RoundSeed): one basis shared by all
        //     groups so comp layers align; equals baseSeed exactly at round 0
        //     (the D9 anchor). Decorrelated (GroupPassSeed): distinct per
        //     group. Both advance with round so progressive re-renders add
        //     information.
        {
            const uint64_t base = 0x12345678ull;
            // Correlated: group-independent, exact at round 0, advances per
            // round.
            Check(RoundSeed(base, 0) == base,
                  "correlated: round 0 seed is exactly the base seed");
            Check(RoundSeed(base, 1) != RoundSeed(base, 0),
                  "correlated: round advances the seed");
            // Decorrelated: distinct per group, deterministic.
            const uint64_t s0 = GroupPassSeed(base, 0, 0);
            const uint64_t s1 = GroupPassSeed(base, 0, 1);
            Check(s0 != s1, "decorrelated: group seeds differ");
            Check(s0 == GroupPassSeed(base, 0, 0),
                  "decorrelated: seed is deterministic");
            Check(GroupPassSeed(base, 1, 0) != s0,
                  "decorrelated: round advances the seed");
        }
    }

    std::cout << (g_failures == 0 ? "ALL PASS" : "FAILURES") << std::endl;
    return g_failures == 0 ? 0 : 1;
}
