/***************************************************************************
 *   This file is part of LuxRender.                                       *
 *                                                                         *
 *   To the extent possible under law, the author(s) have dedicated all    *
 *   copyright and related neighboring rights to the below code to the     *
 *   public domain worldwide. The below code is distributed without any    *
 *   warranty.                                                             *
 *                                                                         *
 *   See: <https://creativecommons.org/publicdomain/zero/1.0/>             *
 ***************************************************************************/

// Phase 3 step 6/8: regression guard for the Null-collapse walker.
//
// A Null surface is optically nothing, so path on a scene with a null occluder
// must equal path on the same scene with no occluder at all -- byte-for-byte,
// since collapse consumes no RNG. This is the strong invariant that caught the
// maxt bug: Intersect narrows ray.maxt to the hit distance, and if Advance does
// not restore it on a Null continuation, geometry behind the Null is culled and
// null-crossing lanes wrongly escape (brightening the image ~5%).
//
// Method: render N independent seeds of each scene and form the paired
// difference d_s = mean_null(s) - mean_empty(s). Same basis per pair, so shared
// randomness cancels. Under the invariant the z-statistic
//     z = mean(d) / (stddev(d) / sqrt(N))
// must stay near zero. The oracle (path_legacy) used during the refactor has
// been deleted; this self-contained check is the permanent regression guard.

#include "core/color/color.h"
#include "core/dynload.h"
#include "core/film.h"
#include "core/paramset.h"
#include "core/scene.h"
#include "core/color/spectrum.h"
#include "integrators/path.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace lux2;

namespace
{
int g_failures = 0;

void Check(bool cond, const char *what)
{
    if (!cond)
    {
        std::cerr << "  FAIL: " << what << std::endl;
        ++g_failures;
    }
    else
    {
        std::cout << "  ok:   " << what << std::endl;
    }
}

// A film that folds each lane's spectral radiance into a luminance sum, like
// path_check's LuminanceProbe. Mean over active splats is the scene estimate.
class LuminanceProbe : public Film
{
  public:
    LuminanceProbe(int xres, int yres) : m_xres(xres), m_yres(yres) {}
    int XRes() const override { return m_xres; }
    int YRes() const override { return m_yres; }
    void Splat(const FloatP &, const FloatP &, const SWCSpectrumP &L,
               const SpectrumWavelengthsP &sw, const FloatP &alpha,
               const FloatP &weight) override
    {
        const FloatP lum = SWCY(L, sw) * weight;
        const MaskP active = alpha > FloatP(0.f);
        float lumArr[PACKET_WIDTH];
        uint32_t actArr[PACKET_WIDTH];
        enoki::store_unaligned(lumArr, lum);
        enoki::store_unaligned(actArr, active);
        for (size_t i = 0; i < PACKET_WIDTH; ++i)
            if (actArr[i] != 0u && enoki::isfinite(lumArr[i]))
            {
                m_sum += double(lumArr[i]);
                m_count += 1.0;
            }
    }
    void Merge(Film *other) override
    {
        if (auto *o = dynamic_cast<LuminanceProbe *>(other))
        {
            m_sum += o->m_sum;
            m_count += o->m_count;
        }
    }
    bool WriteImage(ImageType) override { return true; }
    void AddSampleCount(double) override {}
    double SampleCount() const { return m_count; }
    double Mean() const { return m_count > 0.0 ? m_sum / m_count : 0.0; }

  private:
    int m_xres, m_yres;
    double m_sum = 0.0, m_count = 0.0;
};

// Build a scene: an emissive sphere behind a front plane of material `frontMat`
// ("matte" or "null"), viewed head-on. With "null" the camera ray passes
// through the front plane to the emitter -- exactly the Cornell-Lamp topology
// that the collapse changes. `xr x yr` resolution; `spp` samples per pixel.
// addOccluder=false builds the room+emitter with no front object: the ground
// truth a null occluder must match (a null surface is optically nothing).
std::unique_ptr<Scene> MakeScene(const std::string &frontMat, int xr, int yr,
                                 int spp, bool addOccluder = true)
{
    SceneDescription d;
    d.filmParams.AddInt("xresolution", &xr, 1);
    d.filmParams.AddInt("yresolution", &yr, 1);
    d.samplerParams.AddInt("count", &spp, 1);
    // One light group so the area light lands in GroupLights(0). Without it
    // groupCount is 0 and no light reaches the per-group NEE/env tables.
    d.lightGroups = std::vector<std::string>{"default"};

    // Camera inside a large hollow room sphere (radius 10 at origin), looking
    // at the -z wall. The camera at z=4 is well inside, so every ray hits the
    // room's inner surface -- no enclosing-sphere degeneracy.
    d.cameraName = "perspective";
    d.cameraTransform =
        Transform::look_at(Point3f(0.f, 0.f, 4.f), Point3f(0.f, 0.f, -1.f),
                           Vector3f(0.f, 1.f, 0.f));

    // Room: a large matte sphere (always diffuse) so there is a visible,
    // shaded backdrop behind the occluder. reverseOrientation faces the inner
    // wall toward the camera inside it.
    {
        ShapeDesc sd;
        sd.name = "sphere";
        sd.toWorld = Transform();
        float r = 10.f;
        sd.params.AddFloat("radius", &r, 1);
        sd.reverseOrientation = true;
        sd.material.isNamed = true;
        ParamSet m;
        const std::string matteType = "matte";
        m.AddString("type", &matteType, 1);
        const RGBColor kd(0.5f, 0.5f, 0.5f);
        m.AddRGBColor("Kd", &kd, 1);
        d.namedMaterials["room"] = m;
        sd.material.namedRef = "room";
        d.shapes.push_back(sd);
    }

    // Emitter: a small white area sphere off to one side, lighting the room.
    {
        ShapeDesc sd;
        sd.name = "sphere";
        sd.toWorld = Transform::translate(Vector3f(2.f, 2.f, 2.f));
        float r = 0.5f;
        sd.params.AddFloat("radius", &r, 1);
        sd.isAreaLight = true;
        sd.areaLightName = "area";
        const RGBColor leRGB(5.f, 5.f, 5.f);
        sd.areaLightParams.AddRGBColor("L", &leRGB, 1);
        const float zero = 0.f;
        sd.areaLightParams.AddFloat("power", &zero, 1);
        sd.areaLightParams.AddFloat("efficacy", &zero, 1);
        d.shapes.push_back(sd);
    }

    // A small occluder sphere directly in the camera's view, made of
    // `frontMat`. With "matte" it is a visible diffuse ball (no Null). With
    // "null" the primary ray passes through it to the room wall behind: legacy
    // records it as a Null vertex (consuming a bounce and shifting the RNG
    // stream that shades the wall), path collapses it. This is the topology
    // whose per-seed difference the z-test measures. Skipped when addOccluder
    // is false, giving the no-object ground truth.
    if (addOccluder)
    {
        ShapeDesc sd;
        sd.name = "sphere";
        sd.toWorld = Transform::translate(Vector3f(0.f, 0.f, 0.f));
        float r = 1.f;
        sd.params.AddFloat("radius", &r, 1);
        sd.material.isNamed = true;
        d.namedMaterials["occl"] = [&] {
            ParamSet m;
            m.AddString("type", &frontMat, 1);
            if (frontMat == "matte")
            {
                const RGBColor kd(0.5f, 0.5f, 0.5f);
                m.AddRGBColor("Kd", &kd, 1);
            }
            return m;
        }();
        sd.material.namedRef = "occl";
        d.shapes.push_back(sd);
    }

    auto scene = std::make_unique<Scene>();
    scene->Commit(d);
    return scene;
}

// Render one seed into a fresh probe. sampleBasis selects the per-pixel RNG
// stream, so distinct bases give independent Monte-Carlo estimates.
double RunSeed(SurfaceIntegrator &integ, Scene &scene, uint64_t basis)
{
    integ.Start(scene);
    Tile t;
    t.x0 = 0;
    t.y0 = 0;
    t.x1 = scene.GetFilm().XRes();
    t.y1 = scene.GetFilm().YRes();
    LuminanceProbe probe(scene.GetFilm().XRes(), scene.GetFilm().YRes());
    auto sampler = scene.GetSampler().Clone();
    integ.RenderTile(scene, t, probe, *sampler, basis, sampler->SampleCount());
    return probe.Mean();
}

std::shared_ptr<SurfaceIntegrator> Make(const char *name, int maxdepth)
{
    auto &reg = DynamicLoader::registeredSurfaceIntegrators();
    auto it = reg.find(name);
    if (it == reg.end())
        return nullptr;
    ParamSet params;
    params.AddInt("maxdepth", &maxdepth, 1);
    PluginContext pctx;
    pctx.params = &params;
    return it->second(pctx);
}

// Paired z-test over `nSeeds` independent bases: integrator `aName` on scene
// `sceneA` minus integrator `bName` on scene `sceneB`. Same basis per pair, so
// shared randomness cancels. Returns |z| and prints the summary.
double Sweep(const std::string &label, const std::string &aName, Scene &sceneA,
             const std::string &bName, Scene &sceneB, int nSeeds)
{
    const int maxdepth = 8;
    auto a = Make(aName.c_str(), maxdepth);
    auto b = Make(bName.c_str(), maxdepth);
    if (!a || !b)
    {
        std::cerr << "  integrator not registered\n";
        g_failures++;
        return 1e9;
    }

    std::vector<double> d(nSeeds);
    double sumA = 0.0, sumB = 0.0;
    for (int s = 0; s < nSeeds; ++s)
    {
        const uint64_t basis = uint64_t(s) * 2654435761ull + 12345u;
        const double ma = RunSeed(*a, sceneA, basis);
        const double mb = RunSeed(*b, sceneB, basis);
        d[s] = ma - mb;
        sumA += ma;
        sumB += mb;
    }
    const double meanD = sumA / nSeeds - sumB / nSeeds;
    double var = 0.0;
    for (int s = 0; s < nSeeds; ++s)
    {
        const double e = d[s] - meanD;
        var += e * e;
    }
    const double sd = std::sqrt(var / std::max(1, nSeeds - 1));
    const double se = sd / std::sqrt(double(nSeeds));
    const double z =
        se > 1e-12 ? meanD / se : (std::fabs(meanD) < 1e-9 ? 0.0 : 1e9);

    std::cout << "  [" << label << "] A=" << sumA / nSeeds << " B="
              << sumB / nSeeds << " diff=" << meanD << " sd=" << sd
              << " |z|=" << std::fabs(z) << "\n";
    return std::fabs(z);
}

} // namespace

int main()
{
    std::cout << "lux2 path_collapse_check (Phase 3 step 6)" << std::endl;

    const int nSeeds = 128;
    const int xr = 16, yr = 16, spp = 32;

    auto nullScene = MakeScene("null", xr, yr, spp);
    // Ground truth: the same room with no object. A null surface is optically
    // nothing, so path on nullScene must equal path on emptyScene byte-for-byte.
    auto emptyScene = MakeScene("matte", xr, yr, spp, /*addOccluder=*/false);

    // Collapse is correct: a null occluder changes nothing. If maxt is not
    // restored on a Null continuation, null-crossing lanes escape and this
    // diverges (the bug this guard exists to catch). |z| must stay tiny.
    Check(Sweep("null path-vs-empty path", "path", *nullScene, "path",
                *emptyScene, nSeeds) < 3.5,
          "path(null) == path(no object): collapse is unbiased (|z| < 3.5)");

    if (g_failures)
    {
        std::cerr << g_failures << " collapse check(s) failed" << std::endl;
        return 1;
    }
    std::cout << "collapse checks passed" << std::endl;
    return 0;
}
