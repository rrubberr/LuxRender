// Phase 0 gate: a 1D Energy Redistribution integrator with ZERO renderer
// dependency. It drives the same StepChain()/NumChains()/AcceptProb() the
// packet driver uses, on a spiky integrand binned into "pixels", and checks the
// result against the exact bin integrals.
//
//   build:  g++ -std=c++20 -O2 -fsanitize=address,undefined -I<src root> erpt_1d.cpp
//   expect: every POSITIVE test passes, every NEGATIVE control FAILS (a test
//           that cannot fail is not a gate).

#include "integrators/erpt/chain.h"
#include "integrators/erpt/erpt_math.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <vector>

using namespace lux2;
using namespace lux2::erpt;

namespace
{
    constexpr int kBins = 32;

    // Spiky integrand on (0,1]: baseline + three narrow Gaussians + a hump.
    double F(double x)
    {
        auto g = [](double x, double mu, double s, double a)
        { const double z = (x - mu) / s; return a * std::exp(-0.5 * z * z); };
        return 0.2 + g(x, 0.20, 0.008, 25.0) + g(x, 0.55, 0.010, 18.0) +
               g(x, 0.80, 0.006, 30.0) + g(x, 0.40, 0.12, 1.5);
    }

    std::vector<double> ExactBins()
    {
        std::vector<double> b(kBins, 0.0);
        const int sub = 200000;
        for (int i = 0; i < kBins; ++i)
        {
            double s = 0.0;
            for (int j = 0; j < sub; ++j)
            {
                const double x = (double(i) + (double(j) + 0.5) / sub) / kBins;
                s += F(x);
            }
            b[i] = s / sub / kBins;
        }
        return b;
    }

    enum class Kernel { UniformBox, LogNormal, LogNormalWrongT };
    enum class SeedDist { Uniform, Linear }; // p(x)=1 or p(x)=2x

    struct Cfg
    {
        Kernel kernel = Kernel::UniformBox;
        SeedDist seeds = SeedDist::Uniform;
        bool split = true;
        uint32_t maxConsec = 0;
        uint32_t m = 50;
        double mutationsPerSeed = 50.0; // e_d = e_ave / this
        int N = 3000;                   // seeds per run
    };

    int BinOf(double x)
    {
        int b = int(x * kBins);
        return b < 0 ? 0 : (b >= kBins ? kBins - 1 : b);
    }

    struct RunOut
    {
        std::vector<double> img;
        double energyIn = 0, energyOut = 0;
        uint64_t chains = 0, steps = 0;
        double expectedOut = 0;
    };

    RunOut RunOnce(const Cfg &c, uint32_t run, double eAve)
    {
        RunOut o;
        o.img.assign(kBins, 0.0);
        ChainParams P;
        P.m = c.m;
        P.ed = float(eAve / c.mutationsPerSeed);
        P.maxConsec = c.maxConsec;
        P.splitDeposit = c.split;

        for (int s = 0; s < c.N; ++s)
        {
            Pcg32 sr = KeyedPcg(run, uint32_t(s), 0u);
            double x0, p;
            if (c.seeds == SeedDist::Uniform) { x0 = sr.NextFloat(); p = 1.0; }
            else { x0 = std::sqrt(double(sr.NextFloat())); p = 2.0 * x0; }
            if (x0 <= 0.0 || x0 > 1.0) continue;
            const double e = F(x0) / p;
            o.energyIn += e;
            bool clamped;
            const uint32_t nc = NumChains(float(e), P.m, P.ed, sr.NextFloat(),
                                          P.maxChainsPerSeed, &clamped);
            for (uint32_t ch = 0; ch < nc; ++ch)
            {
                Pcg32 r = KeyedPcg(run, uint32_t(s), ch + 1u);
                double x = x0, fx = F(x);
                ConsecFilter cf;
                cf.Reset({BinOf(x), 0});
                ++o.chains;
                for (uint32_t j = 0; j < P.m; ++j)
                {
                    ++o.steps;
                    double y; float tRatio = 1.f;
                    if (c.kernel == Kernel::UniformBox)
                        y = x + (double(r.NextFloat()) - 0.5) * 0.10;
                    else
                    {
                        // Box-Muller; y = x * exp(0.3 z).  T(y->x)/T(x->y) = y/x.
                        const double u1 = std::max(1e-12, double(r.NextFloat()));
                        const double z = std::sqrt(-2.0 * std::log(u1)) *
                                         std::cos(6.283185307179586 * double(r.NextFloat()));
                        y = x * std::exp(0.3 * z);
                        tRatio = (c.kernel == Kernel::LogNormal) ? float(y / x) : 1.f;
                    }
                    StepIn in;
                    in.x = {BinOf(x), 0};
                    in.u = r.NextFloat();
                    if (y > 0.0 && y <= 1.0)
                    {
                        const double fy = F(y);
                        in.valid = fy > 0.0;
                        in.a = AcceptProb(float(fx), float(fy), tRatio);
                        in.y = {BinOf(y), 0};
                    }
                    const StepOut so = StepChain(P, cf, in);
                    for (int d = 0; d < so.nDep; ++d)
                    {
                        o.img[so.depPx[d].x] += so.depAmount[d];
                        o.energyOut += so.depAmount[d];
                    }
                    if (so.accept) { x = y; fx = F(y); }
                }
            }
        }
        o.expectedOut = double(o.chains) * P.m * P.ed;
        for (double &v : o.img) v /= c.N; // per-sample normalisation (film invP)
        return o;
    }

    // z-score summary across R independent runs vs the exact bins.
    struct Verdict { double maxAbsZ, meanZ2; };
    Verdict Compare(const Cfg &c, int R, const std::vector<double> &exact, double eAve)
    {
        std::vector<std::vector<double>> imgs;
        for (int r = 0; r < R; ++r) imgs.push_back(RunOnce(c, 1000u + r, eAve).img);
        double maxZ = 0, sumZ2 = 0; int n = 0;
        for (int b = 0; b < kBins; ++b)
        {
            double mean = 0; for (auto &im : imgs) mean += im[b]; mean /= R;
            double var = 0; for (auto &im : imgs) var += (im[b] - mean) * (im[b] - mean);
            var /= (R - 1);
            const double se = std::sqrt(var / R);
            if (se <= 0) continue;
            const double z = (mean - exact[b]) / se;
            maxZ = std::max(maxZ, std::fabs(z)); sumZ2 += z * z; ++n;
        }
        return {maxZ, sumZ2 / std::max(1, n)};
    }

    int g_fail = 0;
    void Expect(bool ok, const char *what)
    {
        std::printf("  [%s] %s\n", ok ? " ok " : "FAIL", what);
        if (!ok) ++g_fail;
    }
} // namespace

int main()
{
    const std::vector<double> exact = ExactBins();
    double eAve = 0; for (double b : exact) eAve += b; // = integral of f over [0,1]
    std::printf("exact integral = %.6f\n", eAve);

    const int R = 24;
    struct Case { const char *name; Cfg cfg; bool expectPass; };
    std::vector<Case> cases;
    { Cfg c; cases.push_back({"uniform box, uniform seeds, split deposit", c, true}); }
    { Cfg c; c.split = false; cases.push_back({"uniform box, uniform seeds, Bernoulli deposit (paper-literal)", c, true}); }
    { Cfg c; c.seeds = SeedDist::Linear; cases.push_back({"uniform box, p(x)=2x seeds (e = f/p)", c, true}); }
    { Cfg c; c.kernel = Kernel::LogNormal; cases.push_back({"asymmetric log-normal kernel WITH T ratio", c, true}); }
    { Cfg c; c.kernel = Kernel::LogNormalWrongT; cases.push_back({"NEGATIVE: asymmetric kernel, T ratio omitted", c, false}); }

    for (const Case &k : cases)
    {
        const Verdict v = Compare(k.cfg, R, exact, eAve);
        std::printf("%s\n    max|z| = %.2f, mean z^2 = %.2f\n", k.name, v.maxAbsZ, v.meanZ2);
        const bool pass = v.maxAbsZ < 5.0 && v.meanZ2 < 2.0;
        Expect(pass == k.expectPass, k.expectPass ? "matches exact integral" : "is detected as biased");
    }

    // Energy bookkeeping: without the consecutive filter, split deposit
    // conserves energy exactly: out == chains * m * e_d (every step deposits
    // e_d*a + e_d*(1-a)).
    {
        Cfg c; const RunOut o = RunOnce(c, 7u, eAve);
        const double rel = std::fabs(o.energyOut - o.expectedOut) / o.expectedOut;
        std::printf("energy out %.6g vs chains*m*ed %.6g (rel %.2e)\n", o.energyOut, o.expectedOut, rel);
        Expect(rel < 1e-4, "split deposit conserves chain energy exactly");
    }
    // E[numChains]*m*ed == e.
    {
        const uint32_t m = 40; const float ed = 0.37f; const float e = 5.123f;
        Pcg32 r = KeyedPcg(1, 2, 3); double tot = 0; const int T = 400000; bool cl;
        for (int i = 0; i < T; ++i) tot += NumChains(e, m, ed, r.NextFloat(), 1u << 20, &cl);
        const double rel = std::fabs(tot / T * m * ed - e) / e;
        std::printf("E[numChains]*m*ed = %.4f (e = %.4f)\n", tot / T * m * ed, e);
        Expect(rel < 0.005, "stochastic chain count preserves expected energy");
    }
    // Consecutive filter: gates ONLY the stay-share, and only past the cap.
    {
        ChainParams P; P.ed = 1.f; P.maxConsec = 3; ConsecFilter cf; cf.Reset({5, 5});
        StepIn in; in.x = {5, 5}; in.y = {9, 9}; in.valid = true; in.a = 0.f; in.u = 0.5f;
        int stayDeposits = 0;
        for (int i = 0; i < 10; ++i)
        {
            const StepOut so = StepChain(P, cf, in);
            for (int d = 0; d < so.nDep; ++d) if (so.depPx[d] == Pixel{5, 5}) ++stayDeposits;
        }
        Expect(stayDeposits == 3, "stuck chain deposits at most maxConsec times on one pixel");
        // Moving to a new pixel resets the run.
        in.x = {6, 6};
        const StepOut so = StepChain(P, cf, in);
        Expect(so.nDep == 1 && so.depPx[0] == Pixel{6, 6}, "run length resets on a new pixel");
    }

    std::printf("\n%s (%d failure%s)\n", g_fail ? "GATE FAILED" : "GATE PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
