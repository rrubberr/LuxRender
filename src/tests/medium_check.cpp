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

// Stage 0 medium interface + estimator verification.
//
// Two independent goals:
//   1. Scalar CPU reference of the tracking estimators (D2b flight, D5/P1.3
//      emission, P1.5 ratio-tracking transmittance, majorant conservativeness).
//      Pure scalar math with std::mt19937 — validates the estimator formulas
//      before any SoA medium exists.
//   2. Enoki masked-loop pattern compiles and runs: a mutable SoA walk advanced
//      under enoki::masked, the exact shape the tracked flight/transmittance
//      loops use (P1.4/P1.5). Also includes medium.h / volumefield.h /
//      medium_call.h so the interface is parsed by the compiler.
//
// Built and run as the lux2mediumcheck target / ctest.

#include "core/medium.h"
#include "core/volumefield.h"
#include "core/medium_call.h"
#include "core/mediumptr_table.h"
#include "core/tracked.h"
#include "core/bsdf.h"
#include "core/bsdf_call.h"
#include "core/phase.h"
#include "core/lux2.h"
#include "core/math/math.h"
#include "core/rng.h"
#include "media/homogeneous.h"
#include "media/regionset.h"

// VertexP / EvalVertex / SampleVertex under test. Pulls in the dispatch
// headers itself; included here so the operator-> specializations are visible.
#include "integrators/core/vertex.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <random>
#include <vector>

using namespace lux2;

namespace {

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

// Relative closeness.
bool Close(float a, float b, float eps = 1e-4f)
{
    const float d = std::fabs(a - b);
    const float m = std::max(1.f, std::max(std::fabs(a), std::fabs(b)));
    return d <= eps * m;
}

// Scalar lane access for a packet component.
template <typename P>
auto lane(const P &v, size_t i) { return v[i]; }

// ---------------------------------------------------------------------------
// Scalar reference: homogeneous flight (D2)
// ---------------------------------------------------------------------------
// Sample at rate sigma_s over [0,L]; weight = exp(-sigma_a * d).
// Verify: scatter frequency ~ 1 - exp(-sigma_s L); mean scatter position ~
// truncated-exponential mean; weight matches exp(-sigma_a d).
void CheckHomogeneousFlight()
{
    std::cout << "[scalar] homogeneous flight" << std::endl;
    const float sigma_s = 0.7f, sigma_a = 0.3f, L = 4.f;
    std::mt19937 rng(12345);
    std::uniform_real_distribution<float> U(0.f, 1.f);

    const int N = 400000;
    int scattered = 0;
    double sumPos = 0., sumWeight = 0.;
    for (int i = 0; i < N; ++i)
    {
        const float u = U(rng);
        // Invert CDF of Exp(sigma_s): d = -log(1-u)/sigma_s.
        const float d = -std::log1p(-u) / sigma_s;
        if (d < L)
        {
            ++scattered;
            sumPos += d;
            sumWeight += std::exp(-sigma_a * d);
        }
    }

    const float escProb = 1.f - std::exp(-sigma_s * L);
    const float empScatter = float(scattered) / float(N);
    Check(std::fabs(empScatter - escProb) < 3e-3f,
          "scatter frequency ~ 1-exp(-sigma_s L)");

    // Mean of truncated exponential on [0,L]: (1/sigma_s) - L e^{-s L}/(1-e^{-s L}).
    const float meanPos =
        1.f / sigma_s - L * std::exp(-sigma_s * L) / (1.f - std::exp(-sigma_s * L));
    const float empMeanPos = sumPos / double(scattered);
    Check(std::fabs(empMeanPos - meanPos) < 5e-3f,
          "mean scatter position ~ truncated exponential");

    // E[weight | scattered] = int_0^L sigma_s e^{-s d} e^{-a d} dd /(1-e^{-s L}).
    const float s = sigma_s, a = sigma_a;
    const float expW =
        (s / (s + a)) * (1.f - std::exp(-(s + a) * L)) / (1.f - std::exp(-s * L));
    const float empMeanW = sumWeight / double(scattered);
    Check(std::fabs(empMeanW - expW) < 5e-3f,
          "E[weight|scattered] matches analytic");
}

// ---------------------------------------------------------------------------
// Scalar reference: ratio-tracking transmittance (P1.5)
// ---------------------------------------------------------------------------
// Constant sigma_t with majorant sigma_bar >= sigma_t. Expected T = e^{-s L}.
void CheckRatioTracking()
{
    std::cout << "[scalar] ratio-tracking transmittance" << std::endl;
    const float sigma_t = 0.9f, L = 3.f;
    // Two majorants: tight and loose. Both must give the same expectation (P5.6).
    for (float mult : {1.0f, 2.5f})
    {
        const float sigma_bar = sigma_t * mult;
        std::mt19937 rng(999 + int(mult * 10));
        std::uniform_real_distribution<float> U(0.f, 1.f);

        const int N = 400000;
        double sumT = 0.;
        for (int i = 0; i < N; ++i)
        {
            float t = 0., dist = 0., T = 1.;
            while (true)
            {
                const float step = -std::log1p(-U(rng)) / sigma_bar;
                dist += step;
                if (dist > L)
                    break;
                T *= (1.f - sigma_t / sigma_bar);
                if (T <= 0.f)
                    break;
            }
            sumT += T;
        }
        const float empT = float(sumT / N);
        const float exact = std::exp(-sigma_t * L);
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "ratio-tracking T ~ e^{-sigma_t L} (majorant x%.1f)", mult);
        Check(std::fabs(empT - exact) < 5e-3f, msg);
    }
}

// ---------------------------------------------------------------------------
// Scalar reference: null-collision flight + emission (D2b, D5/P1.3 tracked)
// ---------------------------------------------------------------------------
void CheckTrackedFlightAndEmission()
{
    std::cout << "[scalar] tracked flight + emission" << std::endl;
    const float sigma_t = 0.8f, sigma_s = 0.5f, Lve = 1.3f, L = 3.5f;
    const float sigma_bar = sigma_t * 1.7f; // majorant above sigma_t
    std::mt19937 rng(2468);
    std::uniform_real_distribution<float> U(0.f, 1.f);

    const int N = 400000;
    int scattered = 0;
    double sumPos = 0., sumEmit = 0.;
    for (int i = 0; i < N; ++i)
    {
        float dist = 0., emission = 0.;
        bool didScatter = false;
        while (true)
        {
            const float step = -std::log1p(-U(rng)) / sigma_bar;
            dist += step;
            if (dist > L)
                break; // segment end: escape, weight 1
            emission += Lve / sigma_bar; // every collision emits (null or real)
            if (U(rng) < sigma_t / sigma_bar)
            {
                didScatter = true;
                break; // real collision ends flight
            }
        }
        sumEmit += emission;
        if (didScatter)
        {
            ++scattered;
            sumPos += dist;
        }
    }

    // Scatter position distribution ~ truncated exponential at sigma_t.
    const float meanPos =
        1.f / sigma_t - L * std::exp(-sigma_t * L) / (1.f - std::exp(-sigma_t * L));
    const float empMeanPos = sumPos / double(scattered);
    Check(std::fabs(empMeanPos - meanPos) < 5e-3f,
          "tracked scatter position ~ truncated exponential at sigma_t");

    // E[emission] = int_0^L e^{-sigma_t x} Lve dx = Lve (1-e^{-s L})/s.
    const float exactEmit = Lve * (1.f - std::exp(-sigma_t * L)) / sigma_t;
    const float empEmit = float(sumEmit / N);
    Check(std::fabs(empEmit - exactEmit) < 8e-3f,
          "per-collision Lve/sigma_bar emission ~ integral (P1.3 tracked)");
}

// ---------------------------------------------------------------------------
// Schlick phase: rational lobe (R1). The pre-V3 code implemented the linear
// lobe (1 + k*mu)/(4*pi) with mu = dot(wo, wi), which integrates to 1 for any
// k (so the old normalization test could not fail) but peaks at BACKSCATTER
// for k > 0. The true Schlick lobe is
//     pdf(c) = (1 - k^2) / (4*pi*(1 - k*c)^2),  c = -dot(wo, wi),
// measured from the propagation direction; legacy SchlickScatter writes
// (1 + k*Dot(wo,wi)) because its wo is reversed (schlickscatter.cpp).
// ---------------------------------------------------------------------------

// Legacy scalar reference: SchlickScatter::Pdf with reversed wo.
// cost = Dot(wo, wi); pdf = (1-k^2) / ((1+k*cost)^2 * 4pi).
static float LegacySchlickPdf(float k, float costReversedWo)
{
    const float compcost = 1.f + k * costReversedWo;
    return (1.f - k * k) / (compcost * compcost * 4.f * float(PI));
}

void CheckPhaseNormalization()
{
    std::cout << "[scalar] Schlick phase: normalization, sign, legacy match"
              << std::endl;
    for (float g : {-0.9f, -0.5f, 0.f, 0.5f, 0.9f})
    {
        const float k = SchlickK(g);
        char msg[96];

        // 1. Solid-angle marginal integrates to 1:
        //    int pdf domega = int_{-1}^{1} (1-k^2) / (2*(1-k c)^2) dc
        //    (the 2pi azimuth cancels the 4pi to leave a factor 1/2).
        const int M = 200000;
        double integral = 0.;
        for (int i = 0; i < M; ++i)
        {
            const float c = -1.f + 2.f * (float(i) + 0.5f) / float(M);
            const float den = 1.f - k * c;
            integral += 0.5f * (1.f - k * k) / (den * den) * (2.0 / M);
        }
        snprintf(msg, sizeof(msg), "rational lobe integrates to 1 (g=%.1f)", g);
        Check(std::fabs(float(integral) - 1.f) < 1e-3f, msg);

        // 2. Forward-scatter sign: E[c] must share the sign of k.
        //    (The old linear lobe with mu = dot(wo,wi) had the opposite
        //    convention and peaked at backscatter for g > 0.)
        double meanC = 0.;
        for (int i = 0; i < M; ++i)
        {
            const float c = -1.f + 2.f * (float(i) + 0.5f) / float(M);
            const float den = 1.f - k * c;
            meanC += c * 0.5f * (1.f - k * k) / (den * den) * (2.0 / M);
        }
        snprintf(msg, sizeof(msg), "E[cos] sign matches g (g=%.1f)", g);
        Check((k == 0.f) ? std::fabs(float(meanC)) < 5e-3f
                         : (float(meanC) * k > 0.f),
              msg);
    }

    // 3. Pointwise match against legacy SchlickScatter::Pdf. Broadcast a
    //    fixed (wo, wi) pair to all lanes; hsum/N is the per-lane value.
    {
        const float k = SchlickK(0.7f);
        const Vector3fP wo(0.f, 0.f, 1.f);
        // A non-axis wi so the dot product is exercised.
        const float s = 0.6f, z = std::sqrt(1.f - s * s);
        const Vector3fP wi(s, 0.f, z);
        const FloatP p = SchlickPdf(FloatP(k), wo, wi);
        const float got = enoki::hsum(p) / float(PACKET_WIDTH);
        // Legacy wo is the reversed direction (toward the previous vertex),
        // same convention as ours, and it uses Dot(wo, wi) directly.
        const float costLegacy = 0.f * s + 0.f * 0.f + 1.f * z; // dot(wo, wi)
        const float want = LegacySchlickPdf(k, costLegacy);
        Check(Close(got, want, 1e-5f), "SchlickPdf matches legacy pointwise");
    }
}

// ---------------------------------------------------------------------------
// SoA: SchlickSample pdf equals SchlickPdf at the sampled direction; mean
// forward cosine positive for g > 0. Uses RNGP, the real draw path.
// ---------------------------------------------------------------------------
void CheckPhaseSoASample()
{
    std::cout << "[enoki] Schlick sample/pdf agreement" << std::endl;

    struct BatchStat
    {
        double sumFwd = 0.;
        long n = 0;
    };

    for (float g : {-0.5f, 0.5f, 0.9f})
    {
        const FloatP k(SchlickK(g));
        RNGP rng(0x5EED1234ULL);
        BatchStat st;
        double maxPdfDiff = 0.;

        const Vector3fP wo(0.f, 0.f, 1.f); // toward previous vertex
        const int batches = 2000;
        for (int b = 0; b < batches; ++b)
        {
            const FloatP u1 = rng.NextFloat();
            const FloatP u2 = rng.NextFloat();
            Vector3fP wi(FloatP(0.f));
            FloatP pdf(FloatP(0.f));
            SchlickSample(k, wo, u1, u2, &wi, &pdf);

            // pdf from the sampler must equal the evaluator at wi.
            const FloatP pdfEval = SchlickPdf(k, wo, wi);
            maxPdfDiff = std::max(
                maxPdfDiff,
                double(enoki::hsum(enoki::abs(pdf - pdfEval)) /
                       float(PACKET_WIDTH)));

            // Forward cosine: dot(propagation, wi) with propagation = -wo.
            const FloatP fwd = -dot(wo, wi);
            st.sumFwd += double(enoki::hsum(fwd)) / float(PACKET_WIDTH);
            st.n += PACKET_WIDTH;
        }

        char msg[96];
        snprintf(msg, sizeof(msg),
                 "sample pdf == eval pdf (g=%.1f)", g);
        Check(maxPdfDiff < 1e-5, msg);

        const double meanFwd = st.sumFwd / double(st.n);
        snprintf(msg, sizeof(msg),
                 "mean forward cosine sign matches g (g=%.1f)", g);
        Check(meanFwd * SchlickK(g) > 0.02, msg);
    }

    // Unit-length check on sampled directions.
    {
        RNGP rng(0xABCD0123ULL);
        const Vector3fP wo(FloatP(0.3f), FloatP(-0.5f), FloatP(0.81f));
        const FloatP k(SchlickK(0.4f));
        Vector3fP wi(FloatP(0.f));
        FloatP pdf(FloatP(0.f));
        SchlickSample(k, wo / enoki::norm(wo), rng.NextFloat(),
                      rng.NextFloat(), &wi, &pdf);
        const FloatP len = enoki::norm(wi);
        Check(all(enoki::abs(len - FloatP(1.f)) < FloatP(1e-5f)),
              "sampled directions are unit length");
    }
}

// ---------------------------------------------------------------------------
// Scalar reference: majorant conservativeness on a small grid
// ---------------------------------------------------------------------------
void CheckMajorantBoundScalar()
{
    std::cout << "[scalar] majorant bound (brute force)" << std::endl;
    // A 6x6x6 grid with a sharp spike; verify a coarse max-dilated-by-one
    // bound dominates the trilinear interpolation everywhere.
    const int n = 6;
    std::vector<float> data(n * n * n, 0.f);
    auto idx = [&](int x, int y, int z) { return z * n * n + y * n + x; };
    data[idx(3, 3, 3)] = 5.f; // spike
    data[idx(0, 0, 0)] = 1.f;

    // Trilinear sample (WRAP_BLACK: zero outside [0,1]^3 mapped to grid).
    auto D = [&](int x, int y, int z) {
        x = std::min(std::max(x, 0), n - 1);
        y = std::min(std::max(y, 0), n - 1);
        z = std::min(std::max(z, 0), n - 1);
        return data[idx(x, y, z)];
    };
    auto sample = [&](float px, float py, float pz) {
        if (px < 0.f || px >= 1.f || py < 0.f || py >= 1.f ||
            pz < 0.f || pz >= 1.f)
            return 0.f;
        float vx = px * n - 0.5f, vy = py * n - 0.5f, vz = pz * n - 0.5f;
        int x0 = int(std::floor(vx)), y0 = int(std::floor(vy)), z0 = int(std::floor(vz));
        float dx = vx - x0, dy = vy - y0, dz = vz - z0;
        auto lerp = [](float t, float a, float b) { return a + t * (b - a); };
        float c00 = lerp(dx, D(x0, y0, z0), D(x0 + 1, y0, z0));
        float c10 = lerp(dx, D(x0, y0 + 1, z0), D(x0 + 1, y0 + 1, z0));
        float c01 = lerp(dx, D(x0, y0, z0 + 1), D(x0 + 1, y0, z0 + 1));
        float c11 = lerp(dx, D(x0, y0 + 1, z0 + 1), D(x0 + 1, y0 + 1, z0 + 1));
        float c0 = lerp(dy, c00, c10);
        float c1 = lerp(dy, c01, c11);
        return lerp(dz, c0, c1);
    };

    // Coarse 3x3x3 majorant: max over fine voxels in each coarse cell, dilated
    // by one fine voxel (expand index range by 1 on each side).
    const int res = 3;
    float worstSlack = std::numeric_limits<float>::infinity();
    bool conservative = true;
    for (int i = 0; i < res; ++i)
        for (int j = 0; j < res; ++j)
            for (int l = 0; l < res; ++l)
            {
                // Fine voxel range for this coarse cell, dilated by 1.
                int xlo = std::max(0, i * n / res - 1);
                int xhi = std::min(n - 1, (i + 1) * n / res);
                int ylo = std::max(0, j * n / res - 1);
                int yhi = std::min(n - 1, (j + 1) * n / res);
                int zlo = std::max(0, l * n / res - 1);
                int zhi = std::min(n - 1, (l + 1) * n / res);
                float cellMax = 0.f;
                for (int z = zlo; z <= zhi; ++z)
                    for (int y = ylo; y <= yhi; ++y)
                        for (int x = xlo; x <= xhi; ++x)
                            cellMax = std::max(cellMax, data[idx(x, y, z)]);

                // Sample many points in this coarse cell; assert <= cellMax.
                for (int s = 0; s < 200; ++s)
                {
                    float px = (i + (s % 10 + 0.5f) / 10.f) / res;
                    float py = (j + ((s / 10) % 10 + 0.5f) / 10.f) / res;
                    float pz = (l + (s / 100 + 0.5f) / 10.f) / res;
                    float v = sample(px, py, pz);
                    if (v > cellMax + 1e-5f)
                        conservative = false;
                    worstSlack = std::min(worstSlack, cellMax - v);
                }
            }
    Check(conservative, "dilated coarse majorant bounds trilinear density");
}

// ---------------------------------------------------------------------------
// Enoki masked-loop pattern: mutable SoA state advanced under enoki::masked.
// This is the shape of the tracked flight/transmittance loops (P1.4/P1.5).
// ---------------------------------------------------------------------------
void CheckEnokiMaskedLoop()
{
    std::cout << "[enoki] masked DDA-style loop" << std::endl;

    // Per-lane walk: advance distance by Exp(sigma_bar), accumulate a
    // multiplicative transmittance, terminate on segment end or cap. All
    // mutation is masked; dead lanes must not be touched.
    const FloatP sigmaBar(1.5f);
    const FloatP sigmaT(0.9f);
    const FloatP L(3.f);

    FloatP dist(0.f);
    FloatP T(1.f);
    Int32P collisions(0);
    MaskP active(true);

    const int kMaxIters = 1000;
    int iters = 0;
    for (; iters < kMaxIters && any(active); ++iters)
    {
        // Draw per-lane exponential step (masked so inactive lanes don't
        // consume RNG in the real code; here a deterministic-ish step suffices).
        const FloatP u = FloatP(0.5f) +
                         FloatP(0.4f) * enoki::sin(FloatP(float(iters)) +
                                                  enoki::arange<FloatP>());
        // Invert Exp(sigma_bar): step = -log(1-u)/sigma_bar, guarded u<1-EPS.
        const FloatP uSafe = enoki::min(u, FloatP(1.f) - FloatP(1e-6f));
        const FloatP step = -enoki::log(FloatP(1.f) - uSafe) / sigmaBar;

        // Read-modify-write under the active mask via select (enoki::masked is
        // an assignment proxy, not a value form).
        dist = select(active, dist + step, dist);
        collisions = select(active, collisions + Int32P(1), collisions);

        // Terminate lanes past the segment end.
        const MaskP escape = active && (dist > L);
        active = active && !escape;

        // Accumulate ratio-tracking factor on surviving collisions.
        T = select(active, T * (FloatP(1.f) - sigmaT / sigmaBar), T);
    }

    // Every lane must have terminated within the cap and produced a finite
    // transmittance in [0,1].
    const bool allFinite =
        enoki::all(enoki::isfinite(T) && enoki::isfinite(dist));
    Check(allFinite, "masked loop yields finite per-lane state");

    const bool inRange =
        enoki::all((T >= FloatP(0.f)) && (T <= FloatP(1.f)));
    Check(inRange, "accumulated transmittance stays in [0,1]");

    // The loop must actually terminate (not spin to the cap for all lanes).
    Check(iters < kMaxIters, "masked loop terminates before the iteration cap");
}

// ---------------------------------------------------------------------------
// VertexP consistency (Step 2bc). A minimal Medium double and BSDF double let
// us drive EvalVertex/SampleVertex directly on a MIXED packet — some lanes
// medium (null bsdf), some surface (null med) — to check:
//   1. R6 invariant: on medium lanes SampleVertex's pdf equals EvalVertex's
//      pdf at the sampled direction, and f == 1.
//   2. Pointer-skip: enoki::call never dereferences the null pointer on the
//      opposite class (a crash or garbage here is the failure), and neither
//      class clobbers the other's lanes.
// ---------------------------------------------------------------------------

// Constant-k Schlick medium; PhasePdf is the single-child marginal q.
struct VertexTestMedium : public Medium
{
    float k;
    mutable int phaseCalls = 0;
    explicit VertexTestMedium(float kk) : k(kk) {}

    uint32_t Flags() const override { return MF_Analytic; }
    SWCSpectrumP SigmaA(const SpectrumWavelengthsP &, const Point3fP &,
                        MaskP) const override { return SWCSpectrumP(0.1f); }
    SWCSpectrumP SigmaS(const SpectrumWavelengthsP &, const Point3fP &,
                        MaskP) const override { return SWCSpectrumP(0.9f); }
    void SampleInteraction(const SpectrumWavelengthsP &, const RayP &,
                           const FloatP &, const FloatP &, const FloatP &, RNGP &,
                           MediumSampleP *, MaskP) const override {}
    SWCSpectrumP Transmittance(const SpectrumWavelengthsP &, const RayP &,
                               const FloatP &, const FloatP &, RNGP &,
                               MaskP) const override { return SWCSpectrumP(1.f); }
    FloatP PhasePdf(const SpectrumWavelengthsP &, const Point3fP &,
                    const Vector3fP &wo, const Vector3fP &wi,
                    MaskP active) const override
    {
        ++phaseCalls;
        return select(active, SchlickPdf(FloatP(k), wo, wi), FloatP(0.f));
    }
};

// Diffuse BSDF double: SampleF -> +Z, pdf 0.5, f 0.5; Eval pdf 0.25.
struct VertexTestBSDF : public BSDF
{
    uint32_t flags() const override
    {
        return uint32_t(BSDFType::DiffuseReflection) |
               uint32_t(BSDFType::FrontSide);
    }
    void SampleF(const SpectrumWavelengthsP &, const Vector3fP &,
                 const DifferentialGeometryP &, const FloatP &, const FloatP &,
                 const FloatP &, BSDFSampleP *s, TransportMode,
                 MaskP active) const override
    {
        enoki::masked(s->wi, active) = Vector3fP(FloatP(0.f), FloatP(0.f), FloatP(1.f));
        enoki::masked(s->pdf, active) = FloatP(0.5f);
        enoki::masked(s->f, active) = SWCSpectrumP(FloatP(0.5f));
        enoki::masked(s->sampledType, active) =
            UInt32P(uint32_t(BSDFType::DiffuseReflection));
        enoki::masked(s->specular, active) = MaskP(false);
    }
    FloatP Pdf(const SpectrumWavelengthsP &, const Vector3fP &, const Vector3fP &,
               const DifferentialGeometryP &, uint32_t, TransportMode,
               MaskP active) const override
    {
        return select(active, FloatP(0.25f), FloatP(0.f));
    }
    void Eval(const SpectrumWavelengthsP &, const Vector3fP &, const Vector3fP &,
              const DifferentialGeometryP &, TransportMode, BSDFEvalP *out,
              MaskP active) const override
    {
        enoki::masked(out->f, active) = SWCSpectrumP(FloatP(0.5f));
        enoki::masked(out->pdf, active) = FloatP(0.25f);
        enoki::masked(out->pdfRev, active) = FloatP(0.25f);
    }
};

void CheckVertexConsistency()
{
    std::cout << "[vertex] VertexP sample/eval consistency (mixed packet)"
              << std::endl;

    VertexTestMedium medium(SchlickK(0.5f));
    VertexTestBSDF bsdf;

    // Even lanes: medium vertex (null bsdf). Odd lanes: surface vertex
    // (null med). This is the exact shape path.cpp builds every bounce.
    MaskP isMedium, valid;
    BSDFPtr bsdfPtr(nullptr);
    MediumPtr medPtr(nullptr);
    for (size_t i = 0; i < PACKET_WIDTH; ++i)
    {
        const bool med = (i % 2 == 0);
        isMedium[i] = med;
        valid[i] = true;
        bsdfPtr[i] = med ? nullptr : &bsdf;
        medPtr[i] = med ? &medium : nullptr;
    }

    VertexP vtx;
    vtx.valid = valid;
    vtx.isMedium = isMedium;
    vtx.p = Point3fP(FloatP(0.f), FloatP(0.f), FloatP(0.f));
    DifferentialGeometryP dg;
    dg.p = Point3fP(FloatP(0.f), FloatP(0.f), FloatP(0.f));
    dg.n = Normal3fP(FloatP(0.f), FloatP(0.f), FloatP(1.f));
    dg.ng = Normal3fP(FloatP(0.f), FloatP(0.f), FloatP(1.f));
    dg.entering = MaskP(true);
    dg.uv_u = FloatP(0.f);
    dg.uv_v = FloatP(0.f);
    dg.dp_du = Vector3fP(FloatP(1.f), FloatP(0.f), FloatP(0.f));
    dg.dp_dv = Vector3fP(FloatP(0.f), FloatP(1.f), FloatP(0.f));
    dg.dp_ds = Vector3fP(FloatP(1.f), FloatP(0.f), FloatP(0.f));
    dg.dp_dt = Vector3fP(FloatP(0.f), FloatP(1.f), FloatP(0.f));
    vtx.dg = dg;
    vtx.wo = Vector3fP(FloatP(0.f), FloatP(0.f), FloatP(-1.f));
    vtx.k = FloatP(SchlickK(0.5f));
    vtx.bsdf = bsdfPtr;
    vtx.med = medPtr;

    SpectrumWavelengthsP sw;
    sw.FromWavelength(FloatP(550.f));

    // Sample the continuation direction on the whole mixed packet.
    BSDFSampleP s;
    s.wi = Vector3fP(FloatP(-999.f), FloatP(-999.f), FloatP(-999.f));
    s.pdf = FloatP(-999.f);
    s.f = SWCSpectrumP(FloatP(-999.f));
    SampleVertex(vtx, sw, FloatP(0.37f), FloatP(0.61f), FloatP(0.5f), &s,
                 MaskP(true));

    // Medium lanes: f == 1 and pdf > 0 (the folded q/q). Surface lanes: the
    // stub's SampleF values, proving no cross-class clobber.
    bool medFOk = true, medPdfOk = true, surfOk = true;
    for (size_t i = 0; i < PACKET_WIDTH; ++i)
    {
        if (bool(isMedium[i]))
        {
            medFOk = medFOk && Close(lane(s.f, i), 1.f, 1e-6f);
            medPdfOk = medPdfOk && (lane(s.pdf, i) > 0.f);
        }
        else
        {
            surfOk = surfOk && Close(lane(s.pdf, i), 0.5f, 1e-6f) &&
                     Close(lane(s.f, i), 0.5f, 1e-6f) &&
                     Close(lane(s.wi.z(), i), 1.f, 1e-6f);
        }
    }
    Check(medFOk, "medium lanes: SampleVertex f == 1");
    Check(medPdfOk, "medium lanes: SampleVertex pdf > 0");
    Check(surfOk, "surface lanes: SampleVertex stub values (no clobber)");

    // R6 invariant: EvalVertex at the sampled direction reproduces the
    // medium lane's pdf exactly (same PhasePdf, same args).
    BSDFEvalP ev;
    FloatP cosTerm(FloatP(0.f));
    EvalVertex(vtx, sw, s.wi, &ev, &cosTerm, MaskP(true));

    bool r6Ok = true, medCosOk = true;
    for (size_t i = 0; i < PACKET_WIDTH; ++i)
        if (bool(isMedium[i]))
        {
            r6Ok = r6Ok && Close(lane(ev.pdf, i), lane(s.pdf, i), 1e-5f);
            medCosOk = medCosOk && Close(lane(cosTerm, i), lane(s.pdf, i), 1e-5f);
        }
    Check(r6Ok, "medium lanes: EvalVertex pdf == SampleVertex pdf (R6)");
    Check(medCosOk, "medium lanes: cosTerm == phase q");

    // Surface lanes: EvalVertex ran the BSDF (pdf 0.25), not the phase path.
    bool surfEvalOk = true;
    for (size_t i = 0; i < PACKET_WIDTH; ++i)
        if (!bool(isMedium[i]))
            surfEvalOk = surfEvalOk && Close(lane(ev.pdf, i), 0.25f, 1e-6f);
    Check(surfEvalOk, "surface lanes: EvalVertex BSDF pdf (no phase leak)");
}

// ---------------------------------------------------------------------------
// TrackedLoop (Step 3): drive the real masked loop through a constant-
// coefficient TrackedMedium whose MajorantBegin yields ONE segment with
// sigma_bar = mult * sigma_t. Expectations come from the closed forms the
// scalar references above already use; the point is that the SoA loop, the
// partitioned collision split and the emission accumulation reproduce them.
// ---------------------------------------------------------------------------
struct ForcedTrackedMedium : public TrackedMedium
{
    float sa, ss, Le, mult, k;
    ForcedTrackedMedium(float sa_, float ss_, float Le_, float mult_,
                        float k_ = 0.f)
        : sa(sa_), ss(ss_), Le(Le_), mult(mult_), k(k_) {}

    uint32_t Flags() const override { return MF_Tracked; }
    SWCSpectrumP SigmaA(const SpectrumWavelengthsP &, const Point3fP &,
                        MaskP) const override { return SWCSpectrumP(sa); }
    SWCSpectrumP SigmaS(const SpectrumWavelengthsP &, const Point3fP &,
                        MaskP) const override { return SWCSpectrumP(ss); }
    SWCSpectrumP Lve(const SpectrumWavelengthsP &, const Point3fP &,
                     MaskP) const override { return SWCSpectrumP(Le); }
    FloatP PhasePdf(const SpectrumWavelengthsP &, const Point3fP &,
                    const Vector3fP &wo, const Vector3fP &wi,
                    MaskP active) const override
    {
        return select(active, SchlickPdf(FloatP(k), wo, wi), FloatP(0.f));
    }
    FloatP PhaseK(const SpectrumWavelengthsP &, const Point3fP &, const FloatP &,
                  MaskP active) const override
    {
        return select(active, FloatP(k), FloatP(0.f));
    }

    // One segment spanning [t0, t1]; lanes with an empty interval get
    // sigBar == 0 and escape immediately.
    void MajorantBegin(const RayP &, const FloatP &t0, const FloatP &t1,
                       MajorantState &ms, MaskP active) const override
    {
        const MaskP has = active && (t1 > t0);
        ms.tStart = t0;
        ms.tEnd = select(has, t1, t0);
        ms.sigBar = select(has, FloatP(mult * (sa + ss)), FloatP(0.f));
    }
    MaskP MajorantNext(MajorantState &, MaskP) const override
    {
        return MaskP(false);
    }
};

RayP MakeRay()
{
    return RayP(Point3fP(FloatP(0.f), FloatP(0.f), FloatP(0.f)),
                Vector3fP(FloatP(0.f), FloatP(0.f), FloatP(1.f)),
                FloatP(0.f), FloatP(std::numeric_limits<float>::infinity()),
                FloatP(0.f));
}

void CheckTrackedLoopSoA()
{
    std::cout << "[tracked] TrackedLoop vs closed form (SoA)" << std::endl;

    SpectrumWavelengthsP sw;
    sw.FromWavelength(FloatP(550.f));
    const RayP ray = MakeRay();
    const float L = 3.5f;

    // ---- Flight: terminate-on-absorb, loose majorant -------------------
    const float sa = 0.3f, ss = 0.5f, Le = 1.3f;
    const float sigT = sa + ss;
    for (float mult : {1.0f, 2.5f})
    {
        ForcedTrackedMedium med(sa, ss, Le, mult);
        RNGP rng;
        rng.Seed(0xC0FFEEULL + uint64_t(mult * 1000.f));

        const int kTrials = 60000; // x PACKET_WIDTH lane samples
        double sumW = 0., sumW2 = 0., sumPos = 0., sumEmit = 0.;
        long scattered = 0;
        for (int i = 0; i < kTrials; ++i)
        {
            MediumSampleP ms;
            ClearMediumSample(&ms, FloatP(L));
            med.SampleInteraction(sw, ray, FloatP(0.f), FloatP(L),
                                  rng.NextFloat(), rng, &ms, MaskP(true));
            // Contribution carried to the next vertex: weight when scattered.
            for (size_t l = 0; l < PACKET_WIDTH; ++l)
            {
                const float w = bool(ms.scattered[l]) ? float(ms.weight[l]) : 0.f;
                sumW += w;
                sumW2 += double(w) * w;
                if (bool(ms.scattered[l]))
                {
                    ++scattered;
                    sumPos += float(ms.d[l]);
                }
                sumEmit += float(ms.emission[l]);
            }
        }
        const double N = double(kTrials) * PACKET_WIDTH;

        char msg[160];
        // P(scatter) = (sigma_s/sigma_t)(1 - e^{-sigma_t L}) — the terminate-
        // on-absorb signature (a weighted variant would report 1 - e^{-st L}).
        const float pScat = (ss / sigT) * (1.f - std::exp(-sigT * L));
        const float empScat = float(scattered) / float(N);
        snprintf(msg, sizeof(msg),
                 "flight scatter freq ~ (ss/st)(1-e^-stL) (majorant x%.1f)", mult);
        Check(std::fabs(empScat - pScat) < 4e-3f, msg);

        // E[emission] = int_0^L e^{-st x} Le dx.
        const float exactEmit = Le * (1.f - std::exp(-sigT * L)) / sigT;
        snprintf(msg, sizeof(msg),
                 "flight emission ~ integral (majorant x%.1f)", mult);
        Check(std::fabs(float(sumEmit / N) - exactEmit) < 8e-3f, msg);

        // Position | scatter. For CONSTANT coefficients the scatter/absorb
        // decision is position-independent, so conditioning on scatter rather
        // than on "first real collision" leaves the Exp(sigma_t)-truncated
        // distribution unchanged (scaled by a constant). This does NOT hold
        // for heterogeneous media.
        const float meanPos =
            1.f / sigT - L * std::exp(-sigT * L) / (1.f - std::exp(-sigT * L));
        snprintf(msg, sizeof(msg),
                 "scatter position|scatter ~ Exp(st) truncated (x%.1f)", mult);
        Check(std::fabs(float(sumPos / scattered) - meanPos) < 5e-3f, msg);

        // Loose and tight majorants agree in expectation.
        snprintf(msg, sizeof(msg),
                 "mean weight matches analytic across majorants (x%.1f)", mult);
        const float exactW = pScat; // E[W] with W = weight*1[scattered]
        Check(std::fabs(float(sumW / N) - exactW) < 4e-3f, msg);

        // Variance cost of terminate-on-absorb, measured against the closed
        // form (sample d ~ Exp(ss), weight e^{-sa d}). Same mean, so the
        // variances are directly comparable. Tracked is expected to be a few
        // times higher for this albedo; the band catches gross errors without
        // being flaky.
        const double varTr = sumW2 / N - (sumW / N) * (sumW / N);
        const double eW2 =
            double(ss) * (1.0 - std::exp(-(double(ss) + 2.0 * sa) * L)) /
            (double(ss) + 2.0 * sa);
        const double varCf = eW2 - double(pScat) * double(pScat);
        snprintf(msg, sizeof(msg),
                 "terminate-on-absorb variance finite and within 4x closed form "
                 "(x%.1f: %.3f vs %.3f)",
                 mult, float(varTr), float(varCf));
        Check(varTr > 0.01 && varTr < 4.0 * varCf, msg);
    }

    // ---- Transmittance: ratio tracking + RR ----------------------------
    // sigma_t * L = 3.6 so beta crosses the 0.1 RR threshold: with mult=2.5
    // the per-collision factor is 0.6 and beta < 0.1 after 5 collisions,
    // while the expected collision count over the segment is sigma_bar*L = 9.
    {
        const float stOnly = 1.2f;
        for (float mult : {1.0f, 2.5f})
        {
            ForcedTrackedMedium med(0.f, stOnly, 0.f, mult);
            RNGP rng;
            rng.Seed(0xBEEFULL + uint64_t(mult * 1000.f));

            const int kTrials = 60000;
            double sumT = 0.;
            for (int i = 0; i < kTrials; ++i)
            {
                const SWCSpectrumP T = med.Transmittance(
                    sw, ray, FloatP(0.f), FloatP(L), rng, MaskP(true));
                for (size_t l = 0; l < PACKET_WIDTH; ++l)
                    sumT += float(T[l]);
            }
            const double N = double(kTrials) * PACKET_WIDTH;
            const float exact = std::exp(-stOnly * L);
            char msg[160];
            snprintf(msg, sizeof(msg),
                     "ratio-tracking T ~ e^{-st L} with RR (x%.1f: got %.5f, "
                     "want %.5f)",
                     mult, float(sumT / N), exact);
            Check(std::fabs(float(sumT / N) - exact) < 5e-3f, msg);
        }
        // The RR branch is only meaningful if beta actually reaches the
        // threshold: ln(0.1)/ln(1 - st/sigma_bar) < sigma_bar * L.
        const float sb = 2.5f * stOnly;
        const float kToRR = std::log(0.1f) / std::log(1.f - stOnly / sb);
        Check(kToRR < sb * L,
              "transmittance parameters exercise the RR branch");
#ifdef LUX2_MEDIUM_CAP_COUNTERS
        // Hard proof, when built with counters: the RR kill path ran.
        Check(TrackedMedium::g_trackRRKills > 0,
              "transmittance RR branch actually fired");
#endif
    }

    // ---- Partially active packet with garbage on dead lanes ------------
    // The first real masked loop: dead lanes must not be mutated and their
    // garbage inputs must not leak NaN into live lanes.
    {
        ForcedTrackedMedium med(sa, ss, Le, 2.5f);
        RNGP rng;
        rng.Seed(0x5EEDULL);

        MaskP active;
        for (size_t i = 0; i < PACKET_WIDTH; ++i)
            active[i] = (i % 3 != 0); // one third dead

        // Garbage segment bounds on the dead lanes.
        FloatP t1L;
        const float nan = std::numeric_limits<float>::quiet_NaN();
        for (size_t i = 0; i < PACKET_WIDTH; ++i)
            t1L[i] = active[i] ? L : nan;

        bool finiteOk = true, deadOk = true, liveOk = true;
        for (int i = 0; i < 20000; ++i)
        {
            MediumSampleP ms;
            ClearMediumSample(&ms, FloatP(-7.f));
            med.SampleInteraction(sw, ray, FloatP(0.f), t1L, rng.NextFloat(),
                                  rng, &ms, active);
            for (size_t l = 0; l < PACKET_WIDTH; ++l)
            {
                if (active[l])
                {
                    finiteOk = finiteOk && std::isfinite(float(ms.d[l])) &&
                               std::isfinite(float(ms.weight[l])) &&
                               std::isfinite(float(ms.emission[l]));
                    liveOk = liveOk && (float(ms.d[l]) <= L + 1e-4f);
                }
                else
                {
                    // Untouched: the ClearMediumSample sentinel survives.
                    deadOk = deadOk && (float(ms.d[l]) == -7.f) &&
                             (float(ms.weight[l]) == 1.f);
                }
            }
        }
        Check(finiteOk, "partial-active: live lanes stay finite (garbage dead)");
        Check(deadOk, "partial-active: dead lanes not mutated");
        Check(liveOk, "partial-active: scatter distance within segment");
    }
}

// MediumSampleP::d is the distance from t0, not an absolute ray parameter
// (path.cpp evaluates ray(mint + d)). With a nonzero t0 the walk must return
// d in [0, t1-t0] and E[d | scatter] equal to the truncated-exponential mean
// measured from t0.
void CheckTrackedDistanceRelativeToT0()
{
    std::cout << "[tracked] MediumSampleP::d is relative to t0" << std::endl;

    SpectrumWavelengthsP sw;
    sw.FromWavelength(FloatP(550.f));
    const RayP ray = MakeRay();
    const float t0 = 2.0f, L = 3.5f; // segment length measured from t0
    const float sa = 0.3f, ss = 0.5f, Le = 0.f;
    ForcedTrackedMedium med(sa, ss, Le, 1.7f);
    RNGP rng;
    rng.Seed(0xDEAD00ULL);

    const int kTrials = 60000;
    long scattered = 0;
    double sumD = 0.;
    bool inRange = true;
    for (int i = 0; i < kTrials; ++i)
    {
        MediumSampleP ms;
        ClearMediumSample(&ms, FloatP(L));
        med.SampleInteraction(sw, ray, FloatP(t0), FloatP(t0 + L),
                              rng.NextFloat(), rng, &ms, MaskP(true));
        for (size_t l = 0; l < PACKET_WIDTH; ++l)
        {
            const float d = float(ms.d[l]);
            inRange = inRange && (d >= -1e-4f) && (d <= L + 1e-4f);
            if (bool(ms.scattered[l]))
            {
                ++scattered;
                sumD += d;
            }
        }
    }
    Check(inRange, "d stays within [0, t1-t0] for a nonzero t0");
    const float meanPos =
        1.f / (sa + ss) -
        L * std::exp(-(sa + ss) * L) / (1.f - std::exp(-(sa + ss) * L));
    Check(std::fabs(float(sumD / scattered) - meanPos) < 5e-3f,
          "E[d | scatter] measured from t0 matches truncated exponential");
}

// ---------------------------------------------------------------------------
// RegionSetMedium (Step 4). Children are bounded HomogeneousMedium; the
// composite sums coefficients over containing children and draws phase from
// the sigma_s-weighted mixture.
// ---------------------------------------------------------------------------

// A child with a local box under toWorld.
static std::shared_ptr<HomogeneousMedium>
MakeChild(const RGBColor &sa, const RGBColor &ss, float g, const RGBColor &Le,
          const BBox &localBox, const Transform &toWorld)
{
    auto m = std::make_shared<HomogeneousMedium>(sa, ss, g, Le);
    m->SetBound(localBox, toWorld);
    return m;
}

// Q1 bound gate: the sw-independent scalar sigma_t bound
//   sigma_bar = maxChannel(sigma_a) + maxChannel(sigma_s)
// must dominate the projected sigma_t(sw) at every wavelength, for disjoint
// channel triples where the buggy maxChannel(sigma_a+sigma_s) would fail.
void CheckRegionMajorantBound()
{
    std::cout << "[region] sw-independent majorant bound (Q1)" << std::endl;

    struct Triple
    {
        RGBColor sa, ss;
    };
    const Triple cases[] = {
        {RGBColor(1.f, 0.f, 0.f), RGBColor(0.f, 1.f, 0.f)}, // disjoint R/G
        {RGBColor(0.f, 1.f, 0.f), RGBColor(0.f, 0.f, 1.f)}, // disjoint G/B
        {RGBColor(1.f, 0.f, 0.f), RGBColor(0.f, 0.f, 1.f)}, // disjoint R/B
        {RGBColor(2.f, 0.1f, 0.f), RGBColor(0.f, 0.f, 3.f)},
        {RGBColor(0.5f, 0.5f, 0.5f), RGBColor(0.2f, 0.9f, 0.1f)},
    };

    bool ok = true;
    // Sweep wavelengths across the sampled range, lanes spread over [start,end).
    for (const auto &t : cases)
    {
        HomogeneousMedium m(t.sa, t.ss, 0.f, RGBColor(0.f));
        const float bound = m.SigmaTMaxRGB();
        // Buggy bound for comparison: maxChannel(sa+ss).
        auto maxCh = [](const RGBColor &c) {
            return std::max(c.r(), std::max(c.g(), c.b()));
        };
        const float buggy = maxCh(RGBColor(t.sa.r() + t.ss.r(),
                                           t.sa.g() + t.ss.g(),
                                           t.sa.b() + t.ss.b()));
        bool violatedBuggy = false;
        for (float wl = WAVELENGTH_START; wl < WAVELENGTH_END; wl += 1.f)
        {
            SpectrumWavelengthsP sw;
            sw.FromWavelength(FloatP(wl));
            const SWCSpectrumP st =
                m.SigmaA(sw, Point3fP(0.f), MaskP(true)) +
                m.SigmaS(sw, Point3fP(0.f), MaskP(true));
            for (size_t l = 0; l < PACKET_WIDTH; ++l)
                if (float(st[l]) > bound + 1e-4f)
                    ok = false;
            for (size_t l = 0; l < PACKET_WIDTH; ++l)
                if (float(st[l]) > buggy + 1e-4f)
                    violatedBuggy = true;
        }
        // The disjoint-channel cases must actually exceed the buggy bound,
        // otherwise the test would pass the buggy version.
        const bool disjoint =
            (t.sa.r() > 0.f && t.ss.g() > 0.f && t.sa.g() == 0.f) ||
            (t.sa.g() > 0.f && t.ss.b() > 0.f && t.sa.b() == 0.f) ||
            (t.sa.r() > 0.f && t.ss.b() > 0.f && t.sa.b() == 0.f);
        if (disjoint && !violatedBuggy)
            ok = false;
    }
    Check(ok, "maxChannel(sa)+maxChannel(ss) bounds sigma_t(sw) everywhere");
}

// Containment runs in LOCAL space: a rotated box must reject points that lie
// inside its world AABB but outside the oriented box (the Q2 bug).
void CheckRegionLocalContainment()
{
    std::cout << "[region] local-space containment under rotation (Q2)"
              << std::endl;

    const BBox local(Point3f(-1.f, -1.f, -1.f), Point3f(1.f, 1.f, 1.f));
    // Rotate 45 degrees about Z.
    const Transform rot = Transform::rotate(Vector3f(0.f, 0.f, 1.f), 45.f);
    auto child = MakeChild(RGBColor(0.f), RGBColor(1.f), 0.f, RGBColor(0.f),
                           local, rot);

    // Point at world (1.4, 0, 0): inside the world AABB (half-extent sqrt2)
    // but OUTSIDE the rotated box (its local x = 1.4*cos45 ~ 0.99 < 1 is
    // inside; use a point clearly outside the oriented box).
    // Local coords of world p under inverse rotation:
    const Transform inv = rot.inverse();
    // A corner of the world AABB that falls outside the oriented box.
    const Point3f worldCorner(1.4f, 1.4f, 0.f);
    const Point3f localOfCorner = inv * worldCorner;
    const bool insideWorldAABB = true; // by construction near the diagonal
    (void)insideWorldAABB;

    Point3fP p(FloatP(worldCorner.x()), FloatP(worldCorner.y()),
               FloatP(worldCorner.z()));
    const MaskP contained = child->Contains(p);
    // localOfCorner.x ~ (1.4+1.4)/sqrt2 ~ 1.98 > 1 => outside the box.
    const bool expectOutside =
        !(localOfCorner.x() >= -1.f && localOfCorner.x() <= 1.f &&
          localOfCorner.y() >= -1.f && localOfCorner.y() <= 1.f);
    Check(expectOutside && none(contained),
          "rotated box rejects point inside world AABB but outside box");

    // A point at the box center is contained.
    Point3fP c(FloatP(0.f), FloatP(0.f), FloatP(0.f));
    Check(all(child->Contains(c)), "box center is contained");
}

// Axis-parallel ray inside a box must report a hit (fix #3: the buggy slab
// test forced tf=0 on parallel axes and reported a miss).
void CheckRegionAxisParallel()
{
    std::cout << "[region] axis-parallel ray inside box (fix #3)" << std::endl;

    const BBox local(Point3f(-1.f, -1.f, -1.f), Point3f(1.f, 1.f, 1.f));
    auto child = MakeChild(RGBColor(0.2f), RGBColor(0.8f), 0.f, RGBColor(0.f),
                           local, Transform());
    RegionSetMedium set;
    set.AddChild(child);

    // Ray from the center along +X: parallel to the Y and Z slabs, inside
    // both. Must produce a non-empty interval with sigBar > 0.
    RayP ray(Point3fP(FloatP(0.f), FloatP(0.f), FloatP(0.f)),
             Vector3fP(FloatP(1.f), FloatP(0.f), FloatP(0.f)), FloatP(0.f),
             FloatP(std::numeric_limits<float>::infinity()), FloatP(0.f));
    MajorantState ms;
    set.MajorantBegin(ray, FloatP(0.f),
                      FloatP(std::numeric_limits<float>::infinity()), ms,
                      MaskP(true));
    // Exit at local x=+1 => tEnd ~ 1.
    bool ok = true;
    for (size_t l = 0; l < PACKET_WIDTH; ++l)
        ok = ok && (float(ms.sigBar[l]) > 0.f) &&
             Close(float(ms.tEnd[l]), 1.f, 1e-3f);
    Check(ok, "axis-parallel ray inside box yields a non-empty interval");
}

// A lane missing every box gets zero segments and escapes (anti-hang).
void CheckRegionTermination()
{
    std::cout << "[region] lanes outside every box escape" << std::endl;

    const BBox local(Point3f(-1.f, -1.f, -1.f), Point3f(1.f, 1.f, 1.f));
    auto child = MakeChild(RGBColor(0.2f), RGBColor(0.8f), 0.f, RGBColor(0.f),
                           local, Transform());
    RegionSetMedium set;
    set.AddChild(child);

    // Ray far above the box pointing up: never intersects.
    RayP ray(Point3fP(FloatP(0.f), FloatP(10.f), FloatP(0.f)),
             Vector3fP(FloatP(0.f), FloatP(1.f), FloatP(0.f)), FloatP(0.f),
             FloatP(std::numeric_limits<float>::infinity()), FloatP(0.f));
    MajorantState ms;
    set.MajorantBegin(ray, FloatP(0.f),
                      FloatP(std::numeric_limits<float>::infinity()), ms,
                      MaskP(true));
    bool ok = true;
    for (size_t l = 0; l < PACKET_WIDTH; ++l)
        ok = ok && (float(ms.sigBar[l]) == 0.f);
    Check(ok, "outside ray gets zero segments (sigBar == 0)");
}

// Q3: PhaseK's empirical child-selection frequency must match the sigma_s
// weights used inside PhasePdf. Two overlapping children with different g.
void CheckRegionMixtureConsistency()
{
    std::cout << "[region] PhaseK frequencies match PhasePdf weights (Q3)"
              << std::endl;

    const BBox local(Point3f(-2.f, -2.f, -2.f), Point3f(2.f, 2.f, 2.f));
    // Child A: sigma_s = 0.25, child B: sigma_s = 0.75 -> weights 1:3.
    auto a = MakeChild(RGBColor(0.f), RGBColor(0.25f), -0.9f, RGBColor(0.f),
                       local, Transform());
    auto b = MakeChild(RGBColor(0.f), RGBColor(0.75f), 0.9f, RGBColor(0.f),
                       local, Transform());
    RegionSetMedium set;
    set.AddChild(a);
    set.AddChild(b);

    SpectrumWavelengthsP sw;
    sw.FromWavelength(FloatP(550.f));
    const Point3fP p(FloatP(0.f), FloatP(0.f), FloatP(0.f));
    const float kA = a->K(), kB = b->K();

    RNGP rng;
    rng.Seed(0x1234ABCDULL);
    long pickA = 0, pickB = 0, total = 0;
    const int kTrials = 40000;
    for (int i = 0; i < kTrials; ++i)
    {
        const FloatP u = rng.NextFloat();
        const FloatP k = set.PhaseK(sw, p, u, MaskP(true));
        for (size_t l = 0; l < PACKET_WIDTH; ++l)
        {
            const float kv = float(k[l]);
            if (std::fabs(kv - kA) < 1e-5f)
                ++pickA;
            else if (std::fabs(kv - kB) < 1e-5f)
                ++pickB;
            ++total;
        }
    }
    const float empA = float(pickA) / float(total);
    // Expected weight: 0.25 / (0.25+0.75) = 0.25.
    Check(std::fabs(empA - 0.25f) < 4e-3f,
          "PhaseK child-A frequency ~ sigma_s ratio");

    // PhasePdf marginal at a fixed direction equals the weighted mixture of
    // the two child lobes (same weights).
    const Vector3fP wo(0.f, 0.f, 1.f);
    const Vector3fP wi(0.6f, 0.f, 0.8f);
    const FloatP q = set.PhasePdf(sw, p, wo, wi, MaskP(true));
    const float wA = 0.25f / (0.25f + 0.75f), wB = 1.f - wA;
    const float want =
        wA * float(SchlickPdf(FloatP(kA), wo, wi)[0]) +
        wB * float(SchlickPdf(FloatP(kB), wo, wi)[0]);
    Check(Close(float(q[0]), want, 1e-4f),
          "PhasePdf marginal == sigma_s-weighted mixture of child lobes");
}

// Two coincident regions equal one region with summed coefficients.
void CheckRegionCoincidentSum()
{
    std::cout << "[region] coincident regions sum coefficients" << std::endl;

    const BBox local(Point3f(-2.f, -2.f, -2.f), Point3f(2.f, 2.f, 2.f));
    auto a = MakeChild(RGBColor(0.1f), RGBColor(0.3f), 0.f, RGBColor(0.f),
                       local, Transform());
    auto b = MakeChild(RGBColor(0.2f), RGBColor(0.4f), 0.f, RGBColor(0.f),
                       local, Transform());
    RegionSetMedium set;
    set.AddChild(a);
    set.AddChild(b);

    SpectrumWavelengthsP sw;
    sw.FromWavelength(FloatP(550.f));
    const Point3fP p(FloatP(0.f), FloatP(0.f), FloatP(0.f));
    MediumCoeffs c;
    set.Coeffs(sw, p, MaskP(true), &c);

    // Reference: a single homogeneous with summed RGB.
    HomogeneousMedium sum(RGBColor(0.3f), RGBColor(0.7f), 0.f, RGBColor(0.f));
    const SWCSpectrumP saRef = sum.SigmaA(sw, p, MaskP(true));
    const SWCSpectrumP ssRef = sum.SigmaS(sw, p, MaskP(true));

    bool ok = true;
    for (size_t l = 0; l < PACKET_WIDTH; ++l)
        ok = ok && Close(float(c.sa[l]), float(saRef[l]), 1e-4f) &&
             Close(float(c.ss[l]), float(ssRef[l]), 1e-4f);
    Check(ok, "coincident regions == summed single region");
}

// ---------------------------------------------------------------------------
// Interface smoke test: MediumSampleP clear helper + ConstantField.
// ---------------------------------------------------------------------------
void CheckInterfaceSmoke()
{
    std::cout << "[interface] smoke" << std::endl;

    MediumSampleP ms;
    ClearMediumSample(&ms, FloatP(7.f));
    Check(enoki::all(eq(ms.d, FloatP(7.f))), "ClearMediumSample sets d=t1");
    Check(enoki::all(eq(ms.weight, SWCSpectrumP(1.f))), "clear weight == 1");
    Check(enoki::all(eq(ms.emission, SWCSpectrumP(0.f))), "clear emission == 0");
    Check(none(ms.scattered), "clear scattered == false");

    ConstantField cf(2.5f);
    Point3fP p(FloatP(0.1f), FloatP(-3.f), FloatP(9.f));
    Check(enoki::all(eq(cf.Eval(p, MaskP(true)), FloatP(2.5f))),
          "ConstantField::Eval returns constant");
    Check(Close(cf.Max(), 2.5f), "ConstantField::Max matches");

    MajorantGrid grid;
    cf.BuildMajorantGrid(grid, 4);
    Check(grid.IsValid(), "ConstantField builds a valid majorant grid");
    Check(Close(grid.GlobalMax(), 2.5f), "majorant global max matches");
}

void CheckMediumPtrLaneConsistency()
{
    std::cout << "[mediumptr] lane-consistent medium calls" << std::endl;

    SpectrumWavelengthsP sw; sw.FromWavelength(FloatP(550.f));
    const RayP ray = MakeRay();
    const float L = 3.5f;

    // Deterministic single-segment tracked medium (sa=0.3, ss=0.5, Le=1.3, g=2.5x).
    static ForcedTrackedMedium med(0.3f, 0.5f, 1.3f, 2.5f, 0.0f);
    MediumPtrTable tbl;
    tbl.ptrs.push_back(&med);
    tbl.RebuildNonVacuum();

    const MediumPtr mp = tbl.Gather(UInt32P(uint32_t(0)), MaskP(true));
    const FloatP uFlight = FloatP(0.3125f); // broadcast, identical all lanes

    auto seedSame = [] (RNGP &rng)
    {
        const StreamKeyResult k = StreamKey(UInt32P(7u), UInt32P(3u), Int32P(1),
                                            Int32P(0), UInt32P(0u), FLIGHT_SALT);
        rng.Seed(k.state, k.seq); // broadcast -> identical streams
    };
    // Every active lane's float field must equal the scalar reference.
    auto laneConst = [] (const char *name, const SWCSpectrumP &v,
                         float ref, const MaskP &active)
    {
        for (size_t a = 0; a < PACKET_WIDTH; ++a)
            if (bool(active[a]) && std::fabs(float(v[a]) - ref) > 1e-6f)
            { std::cerr << "  FAIL: " << name << " lane " << a << std::endl; ++g_failures; return; }
    };

    struct Ref { Float w, e, d, k; bool scat, abs; };

    // --- all active: record reference ---
    Ref ref;
    {
        RNGP rng; seedSame(rng);
        MediumSampleP o; ClearMediumSample(&o, FloatP(L));
        mp->SampleInteraction(sw, ray, FloatP(0.f), FloatP(L), uFlight, rng, &o, MaskP(true));
        laneConst("weight all-active", o.weight, float(o.weight[0]), MaskP(true));
        laneConst("emission all-active", o.emission, float(o.emission[0]), MaskP(true));
        ref = { float(o.weight[0]), float(o.emission[0]), float(o.d[0]), float(o.k[0]),
                bool(o.scattered[0]), bool(o.absorbed[0]) };
    }

    // --- alternating 1,0,1,0,... ---
    {
        MaskP alt(false);
        for (size_t i = 0; i < PACKET_WIDTH; i += 2) alt[i] = true;
        RNGP rng; seedSame(rng);
        MediumSampleP o; ClearMediumSample(&o, FloatP(L));
        mp->SampleInteraction(sw, ray, FloatP(0.f), FloatP(L), uFlight, rng, &o, alt);
        // Active lanes == all-active reference; masked lanes == cleared defaults.
        laneConst("weight alternating(active)", o.weight, ref.w, alt);
        laneConst("emission alternating(active)", o.emission, ref.e, alt);
        for (size_t a = 0; a < PACKET_WIDTH; ++a)
            if (bool(!alt[a])) {
                if (std::fabs(float(o.weight[a]) - 1.f) > 1e-6f ||
                    std::fabs(float(o.emission[a]) - 0.f) > 1e-6f) {
                    std::cerr << "  FAIL: masked lane " << a << " not default" << std::endl; ++g_failures;
                }
            }
    }

    // --- first half 1,1,1,1,0,0,0,0 ---
    {
        MaskP half(false);
        for (size_t i = 0; i < PACKET_WIDTH / 2; ++i) half[i] = true;
        RNGP rng; seedSame(rng);
        MediumSampleP o; ClearMediumSample(&o, FloatP(L));
        mp->SampleInteraction(sw, ray, FloatP(0.f), FloatP(L), uFlight, rng, &o, half);
        laneConst("weight first-half(active)", o.weight, ref.w, half);
        laneConst("emission first-half(active)", o.emission, ref.e, half);
        for (size_t a = 0; a < PACKET_WIDTH; ++a)
            if (bool(!half[a])) {
                if (std::fabs(float(o.weight[a]) - 1.f) > 1e-6f ||
                    std::fabs(float(o.emission[a]) - 0.f) > 1e-6f) {
                    std::cerr << "  FAIL: masked lane " << a << " not default" << std::endl; ++g_failures;
                }
            }
    }
    if (g_failures == 0) std::cout << "  ok:   MediumPtr calls lane-consistent" << std::endl;
}

} // namespace

int main()
{
    std::cout << "== lux2mediumcheck (PACKET_WIDTH=" << PACKET_WIDTH << ") =="
              << std::endl;

    CheckInterfaceSmoke();
    CheckVertexConsistency();
    CheckPhaseNormalization();
    CheckPhaseSoASample();
    CheckHomogeneousFlight();
    CheckRatioTracking();
    CheckTrackedFlightAndEmission();
    CheckTrackedLoopSoA();
    CheckTrackedDistanceRelativeToT0();
    CheckMajorantBoundScalar();
    CheckEnokiMaskedLoop();
    CheckRegionMajorantBound();
    CheckRegionLocalContainment();
    CheckRegionAxisParallel();
    CheckRegionTermination();
    CheckRegionMixtureConsistency();
    CheckRegionCoincidentSum();
    CheckMediumPtrLaneConsistency();

    if (g_failures == 0)
    {
        std::cout << "ALL MEDIUM CHECKS PASSED" << std::endl;
        return 0;
    }
    std::cerr << g_failures << " MEDIUM CHECK(S) FAILED" << std::endl;
    return 1;
}
