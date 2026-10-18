/***************************************************************************
 *   This file is part of LuxRender.                                       *
 *                                                                         *
 *   To the extent possible under law, the author(s) have dedicated all    *
 *   copyright and related neighboring rights to the below code to the     *
 *   public domain worldwide. The below code is distributed without any    *
 *   warranty.                                                             *
 *                                                                         *
 *   See: <https://creativecommons.org/publicdomain/zero/1.0/>             *
 *                                                                         *
 ***************************************************************************/

// Verifies power/efficacy photometric normalization for point and area lights,
// plus the scalar illuminant luminance RGBIlluminanceY.
//
// Two independent checks:
//   1. Golden: RGBIlluminanceY vs the colorref RGBIllumSPD integrated Y.
//   2. Flux closure (legacy-independent): the effective emitted luminance
//      times the geometry term equals power*efficacy, measured by Monte
//      Carlo over wavelength of SWCY(LeOut). This exercises the whole chain
//      (IntegratedLuminance -> PhotometricGainFactor -> Sample_Li gain) without
//      referencing legacy numbers.

#include "core/color.h"
#include "core/data/xyzbasis.h"
#include "core/geometry.h"
#include "core/light.h"
#include "core/math.h"
#include "core/shape.h"
#include "core/texture.h"
#include "lights/area.h"
#include "lights/point.h"
#include "textures/constant.h"

#include "color_ref/color_ref.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
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

bool Close(float a, float b, float tol = 2e-3f) {
    return std::fabs(a - b) <= tol * (1.f + std::fabs(a) + std::fabs(b));
}

float RandUnit() { return float(std::rand()) / float(RAND_MAX + 1.f); }

float First(const FloatP &p) {
    float buf[PACKET_WIDTH];
    enoki::store_unaligned(buf, p);
    return buf[0];
}

// Reference luminance: integrate the colorref illuminant SPD against CIE_Y.
float RefIllumY(float r, float g, float b) {
    colorref::RGBIllumSPD spd(colorref::RGBColor(r, g, b));
    double y = 0.0;
    for (unsigned i = 0; i < lux2::nCIE; ++i)
        y += double(spd.SampleAt(float(lux2::CIEstart + i))) * lux2::CIE_Y[i];
    return float(y * 683.0);
}

TriangleDesc QuadTri(const Point3f &a, const Point3f &b, const Point3f &c) {
    TriangleDesc t;
    t.v0 = a; t.v1 = b; t.v2 = c;
    t.n0 = Normal3f(0.f, 0.f, 1.f);
    t.n1 = Normal3f(0.f, 0.f, 1.f);
    t.n2 = Normal3f(0.f, 0.f, 1.f);
    t.matID = 0; t.lightID = 0;
    return t;
}

std::shared_ptr<ConstantColorTexture> IllumTex(const RGBColorP &rgb) {
    return std::make_shared<ConstantColorTexture>(rgb, /*illuminant=*/true);
}

// Monte-Carlo estimate of the luminance Y of a light's emitted radiance by
// averaging SWCY(LeOut) over uniform wavelengths. For an area light LeOut is
// position independent (constant texture); for a point light we sample at a
// fixed distance and divide out the falloff.
float MCLuminance(const std::shared_ptr<Light> &light,
                  const Point3fP &p, const Normal3fP &n, int samples) {
    double acc = 0.0;
    for (int i = 0; i < samples; ++i) {
        SpectrumWavelengthsP sw;
        sw.Sample(FloatP(RandUnit()));
        Point3fP lightP; Vector3fP wi; Normal3fP lightN; FloatP pdf; SWCSpectrumP Le;
        light->Sample_Li(sw, p, n, FloatP(RandUnit()), FloatP(RandUnit()),
                        FloatP(RandUnit()), &lightP, &wi, &lightN, &pdf, &Le);
        acc += double(SWCY(Le, sw)[0]);
    }
    return float(acc / double(samples));
}

} // namespace

int main() {
    std::cout << "lux2 photometric_check" << std::endl;
    std::srand(12345);

    // ---- 1. Golden: RGBIlluminanceY vs colorref illuminant SPD ----------
    {
        const float cols[][3] = {
            {1.f, 1.f, 1.f}, {0.8f, 0.2f, 0.1f},
            {0.1f, 0.9f, 0.3f}, {0.5f, 0.5f, 0.9f},
            {0.f, 0.f, 0.f},
        };
        bool all = true;
        for (const auto &c : cols) {
            const lux2::RGBColor rgb(c[0], c[1], c[2]);
            const float mine = lux2::RGBIlluminanceY(rgb);
            const float ref = RefIllumY(c[0], c[1], c[2]);
            if (!Close(mine, ref, 1e-3f)) {
                std::cerr << "    Y mismatch (" << c[0] << "," << c[1]
                          << "," << c[2] << "): mine=" << mine
                          << " ref=" << ref << std::endl;
                all = false;
            }
        }
        Check(all, "RGBIlluminanceY matches colorref RGBIllumSPD integrated Y");
    }

    // ---- 2. IntegratedLuminance: exact for illuminant, nullopt otherwise -
    {
        auto illum = IllumTex(RGBColorP(FloatP(1.f), FloatP(1.f), FloatP(1.f)));
        auto refl = std::make_shared<ConstantColorTexture>(
            RGBColorP(FloatP(1.f), FloatP(1.f), FloatP(1.f)), false);
        Check(illum->IntegratedLuminance().has_value(),
              "Illuminant constant has IntegratedLuminance");
        Check(!refl->IntegratedLuminance().has_value(),
              "Reflectant constant returns nullopt");
    }

    // ---- 3. Area flux closure: Y_eff * PI*area == power*efficacy ---------
    {
        const float P = 250.f, E = 12.f;
        auto light = std::make_shared<AreaLight>(
            IllumTex(RGBColorP(FloatP(1.f), FloatP(1.f), FloatP(1.f))),
            /*baseGain=*/1.f, P, E);
        // Unit quad (area 1) at z=0.
        const Point3f p00(0, 0, 0), p10(1, 0, 0), p11(1, 1, 0), p01(0, 1, 0);
        light->BindGeometry({QuadTri(p00, p10, p11), QuadTri(p00, p11, p01)});

        Point3fP p(FloatP(0.5f), FloatP(0.5f), FloatP(1.f));
        Normal3fP n(FloatP(0.f), FloatP(0.f), FloatP(-1.f));
        const float Ye = MCLuminance(light, p, n, 200000);
        const float flux = Ye * PI * light->TotalArea();
        Check(Close(flux, P * E, 0.03f),
              "Area flux closure: Y_eff*PI*A == power*efficacy");
    }

    // ---- 4. Area flux closure with non-unit area (scaled quad) ----------
    {
        const float P = 100.f, E = 17.f;
        auto light = std::make_shared<AreaLight>(
            IllumTex(RGBColorP(FloatP(1.f), FloatP(1.f), FloatP(1.f))),
            /*baseGain=*/1.f, P, E);
        // Quad scaled to area 4.
        const Point3f a(0, 0, 0), b(2, 0, 0), c(2, 2, 0), d(0, 2, 0);
        light->BindGeometry({QuadTri(a, b, c), QuadTri(a, c, d)});
        Check(Close(light->TotalArea(), 4.f, 1e-4f), "Scaled area == 4");

        Point3fP p(FloatP(1.f), FloatP(1.f), FloatP(1.f));
        Normal3fP n(FloatP(0.f), FloatP(0.f), FloatP(-1.f));
        const float Ye = MCLuminance(light, p, n, 200000);
        const float flux = Ye * PI * light->TotalArea();
        Check(Close(flux, P * E, 0.03f),
              "Area flux closure holds under scaling");
    }

    // ---- 5. Point flux closure: Y_eff(at d=1) * 4PI == power*efficacy ----
    {
        const float P = 80.f, E = 20.f;
        auto light = std::make_shared<PointLight>(
            IllumTex(RGBColorP(FloatP(1.f), FloatP(1.f), FloatP(1.f))),
            /*baseGain=*/1.f, P, E, Point3f(0.f, 0.f, 0.f));
        // Receiver at distance 1 so LeOut == base*gain (no falloff).
        Point3fP p(FloatP(0.f), FloatP(0.f), FloatP(1.f));
        Normal3fP n(FloatP(0.f), FloatP(0.f), FloatP(-1.f));
        const float Ye = MCLuminance(light, p, n, 200000);
        const float flux = Ye * 4.f * PI;
        Check(Close(flux, P * E, 0.03f),
              "Point flux closure: Y_eff*4PI == power*efficacy");
    }

    // ---- 6. Point falloff: LeOut scales as 1/distSq ---------------------
    {
        auto light = std::make_shared<PointLight>(
            IllumTex(RGBColorP(FloatP(1.f), FloatP(1.f), FloatP(1.f))),
            /*baseGain=*/1.f, /*power=*/0.f, /*efficacy=*/0.f,
            Point3f(0.f, 0.f, 0.f));
        SpectrumWavelengthsP sw;
        sw.FromWavelength(FloatP(550.f));
        Normal3fP n(FloatP(0.f), FloatP(0.f), FloatP(-1.f));
        auto LeAt = [&](float z) {
            Point3fP p(FloatP(0.f), FloatP(0.f), FloatP(z));
            Point3fP lp; Vector3fP wi; Normal3fP ln; FloatP pdf; SWCSpectrumP Le;
            light->Sample_Li(sw, p, n, FloatP(0.5f), FloatP(0.5f), FloatP(0.5f),
                            &lp, &wi, &ln, &pdf, &Le);
            return First(Le);
        };
        const float l1 = LeAt(1.f), l2 = LeAt(2.f);
        Check(Close(l1 / 4.f, l2, 1e-3f), "Point LeOut falls off as 1/distSq");
    }

    // ---- 7. power=0 leaves gain bit-identical to baseGain ---------------
    {
        auto light = std::make_shared<AreaLight>(
            IllumTex(RGBColorP(FloatP(1.f), FloatP(1.f), FloatP(1.f))),
            /*baseGain=*/3.f, /*power=*/0.f, /*efficacy=*/0.f);
        const Point3f a(0, 0, 0), b(1, 0, 0), c(1, 1, 0), d(0, 1, 0);
        light->BindGeometry({QuadTri(a, b, c), QuadTri(a, c, d)});
        SpectrumWavelengthsP sw;
        sw.FromWavelength(FloatP(550.f));
        Point3fP p(FloatP(0.5f), FloatP(0.5f), FloatP(1.f));
        Normal3fP n(FloatP(0.f), FloatP(0.f), FloatP(-1.f));
        Point3fP lp; Vector3fP wi; Normal3fP ln; FloatP pdf; SWCSpectrumP Le;
        light->Sample_Li(sw, p, n, FloatP(0.25f), FloatP(0.5f), FloatP(0.5f),
                        &lp, &wi, &ln, &pdf, &Le);
        // base=RGBToSmitsSPD(white) at 550nm; gain must be exactly 3 (no
        // normalization). Compare against an unnormalized reference light.
        auto ref = std::make_shared<AreaLight>(
            IllumTex(RGBColorP(FloatP(1.f), FloatP(1.f), FloatP(1.f))),
            /*baseGain=*/3.f, 0.f, 0.f);
        ref->BindGeometry({QuadTri(a, b, c), QuadTri(a, c, d)});
        Point3fP lp2; Vector3fP wi2; Normal3fP ln2; FloatP pdf2; SWCSpectrumP Le2;
        ref->Sample_Li(sw, p, n, FloatP(0.25f), FloatP(0.5f), FloatP(0.5f),
                      &lp2, &wi2, &ln2, &pdf2, &Le2);
        Check(First(Le) == First(Le2), "power=0 gain is bit-identical");
    }

    // ---- 8. Black L (Y==0): normalization skipped, no NaN ---------------
    {
        auto light = std::make_shared<AreaLight>(
            IllumTex(RGBColorP(FloatP(0.f), FloatP(0.f), FloatP(0.f))),
            /*baseGain=*/1.f, /*power=*/100.f, /*efficacy=*/17.f);
        const Point3f a(0, 0, 0), b(1, 0, 0), c(1, 1, 0), d(0, 1, 0);
        light->BindGeometry({QuadTri(a, b, c), QuadTri(a, c, d)});
        SpectrumWavelengthsP sw;
        sw.FromWavelength(FloatP(550.f));
        Point3fP p(FloatP(0.5f), FloatP(0.5f), FloatP(1.f));
        Normal3fP n(FloatP(0.f), FloatP(0.f), FloatP(-1.f));
        Point3fP lp; Vector3fP wi; Normal3fP ln; FloatP pdf; SWCSpectrumP Le;
        light->Sample_Li(sw, p, n, FloatP(0.25f), FloatP(0.5f), FloatP(0.5f),
                        &lp, &wi, &ln, &pdf, &Le);
        Check(!std::isnan(First(Le)), "Black L: LeOut not NaN (skip + warn)");
    }

    // ---- 9. Negative power: normalization skipped -----------------------
    {
        auto light = std::make_shared<PointLight>(
            IllumTex(RGBColorP(FloatP(1.f), FloatP(1.f), FloatP(1.f))),
            /*baseGain=*/2.f, /*power=*/-50.f, /*efficacy=*/17.f,
            Point3f(0.f, 0.f, 0.f));
        SpectrumWavelengthsP sw;
        sw.FromWavelength(FloatP(550.f));
        Point3fP p(FloatP(0.f), FloatP(0.f), FloatP(1.f));
        Normal3fP n(FloatP(0.f), FloatP(0.f), FloatP(-1.f));
        Point3fP lp; Vector3fP wi; Normal3fP ln; FloatP pdf; SWCSpectrumP Le;
        light->Sample_Li(sw, p, n, FloatP(0.5f), FloatP(0.5f), FloatP(0.5f),
                        &lp, &wi, &ln, &pdf, &Le);
        // gain must remain baseGain=2 (negative factor rejected).
        auto ref = std::make_shared<PointLight>(
            IllumTex(RGBColorP(FloatP(1.f), FloatP(1.f), FloatP(1.f))),
            /*baseGain=*/2.f, 0.f, 0.f, Point3f(0.f, 0.f, 0.f));
        Point3fP lp2; Vector3fP wi2; Normal3fP ln2; FloatP pdf2; SWCSpectrumP Le2;
        ref->Sample_Li(sw, p, n, FloatP(0.5f), FloatP(0.5f), FloatP(0.5f),
                      &lp2, &wi2, &ln2, &pdf2, &Le2);
        Check(First(Le) == First(Le2), "Negative power skipped (gain unchanged)");
    }

    // ---- 10. Zero-area bind: no crash, pdf 0 ----------------------------
    {
        auto light = std::make_shared<AreaLight>(
            IllumTex(RGBColorP(FloatP(1.f), FloatP(1.f), FloatP(1.f))),
            /*baseGain=*/1.f, /*power=*/100.f, /*efficacy=*/17.f);
        light->BindGeometry({}); // empty
        SpectrumWavelengthsP sw;
        sw.FromWavelength(FloatP(550.f));
        Point3fP p(FloatP(0.5f), FloatP(0.5f), FloatP(1.f));
        Normal3fP n(FloatP(0.f), FloatP(0.f), FloatP(-1.f));
        Point3fP lp; Vector3fP wi; Normal3fP ln; FloatP pdf; SWCSpectrumP Le;
        MaskP valid = light->Sample_Li(sw, p, n, FloatP(0.25f), FloatP(0.5f),
                                      FloatP(0.5f), &lp, &wi, &ln, &pdf, &Le);
        Check(!bool(enoki::any(valid)), "Zero-area bind: no valid samples");
    }

    // ---- 11. Idempotent BindGeometry ------------------------------------
    {
        const float P = 100.f, E = 17.f;
        auto light = std::make_shared<AreaLight>(
            IllumTex(RGBColorP(FloatP(1.f), FloatP(1.f), FloatP(1.f))),
            /*baseGain=*/1.f, P, E);
        const Point3f a(0, 0, 0), b(1, 0, 0), c(1, 1, 0), d(0, 1, 0);
        std::vector<TriangleDesc> tris = {QuadTri(a, b, c), QuadTri(a, c, d)};
        light->BindGeometry(tris);
        SpectrumWavelengthsP sw;
        sw.FromWavelength(FloatP(550.f));
        Point3fP p(FloatP(0.5f), FloatP(0.5f), FloatP(1.f));
        Normal3fP n(FloatP(0.f), FloatP(0.f), FloatP(-1.f));
        Point3fP lp; Vector3fP wi; Normal3fP ln; FloatP pdf; SWCSpectrumP Le1;
        light->Sample_Li(sw, p, n, FloatP(0.25f), FloatP(0.5f), FloatP(0.5f),
                        &lp, &wi, &ln, &pdf, &Le1);
        // Re-bind the same geometry: gain must not accumulate.
        light->BindGeometry(tris);
        Point3fP lp2; Vector3fP wi2; Normal3fP ln2; FloatP pdf2; SWCSpectrumP Le2;
        light->Sample_Li(sw, p, n, FloatP(0.25f), FloatP(0.5f), FloatP(0.5f),
                        &lp2, &wi2, &ln2, &pdf2, &Le2);
        Check(First(Le1) == First(Le2), "BindGeometry is idempotent");
    }

    std::cout << (g_failures == 0 ? "ALL PASS" : "FAILURES")
              << " (" << g_failures << ")" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
