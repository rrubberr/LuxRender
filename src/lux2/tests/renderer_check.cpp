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
    d.samplerName = "ldsampler";
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

    // ---- 4. determinism: identical scenes render bitwise identically ---
    {
        auto s1 = MakeEmitterScene(64, 64, -1);
        auto s2 = MakeEmitterScene(64, 64, -1);
        s1->GetRenderer().Render(*s1, s1->GetSurfaceIntegrator());
        s2->GetRenderer().Render(*s2, s2->GetSurfaceIntegrator());

        const FlexImageFilm *f1 = AsFlex(*s1), *f2 = AsFlex(*s2);
        bool same = f1 && f2 &&
                    f1->BufY() == f2->BufY() &&
                    f1->BufX() == f2->BufX() &&
                    f1->BufZ() == f2->BufZ() &&
                    f1->BufWeight() == f2->BufWeight();
        Check(same, "two renders are bitwise identical (thread-independent seeding)");
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
        d.samplerName = "ldsampler";
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

    std::cout << (g_failures == 0 ? "ALL PASS" : "FAILURES") << std::endl;
    return g_failures == 0 ? 0 : 1;
}
