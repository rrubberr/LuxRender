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

// ERPT chain-logic spike (Phase 0) -- no renderer, no scene, no SIMD.
//
// Validates the *accounting* and *stationarity* of the equal-deposition
// Metropolis chain described in the ERPT paper (Cline/Talbot/Egbert) and
// implemented in Mitsuba-1's erpt_proc.cpp, using a 1D target so the exact
// integral is known. Three independent properties are checked:
//
//   A. Telescoping invariant. With the split-deposit rule (Mitsuba:
//      accumulate 1-a at the current path, deposit a at the proposal on
//      rejection, carry `a` forward on acceptance), the *total* energy a
//      chain deposits over its m steps is exactly m * depositionEnergy,
//      for every accept/reject sequence. This is what makes ERPT's
//      per-pixel estimate independent of where acceptances happen.
//
//   B. Stationarity. The spatial distribution of deposited energy across
//      [0,1] converges to g(x) / integral(g). A biased acceptance rule
//      would distort this histogram even if the total were correct.
//
//   C. Unbiasedness. Sum of all deposits divided by the number of seeds
//      converges to integral(g), given stochastic numChains with
//      E[numChains] == meanChains and depositionEnergy = w/(meanChains*m).
//
// The mutator-specific Q symmetry (detailed balance of the real lens /
// caustic perturbations) is NOT tested here -- that needs a real scene and
// lands in Phase 2/3. This spike isolates the chain driver only.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

namespace
{
int g_failures = 0;

void Check(bool cond, const char *what)
{
    if (!cond)
    {
        std::fprintf(stderr, "  FAIL: %s\n", what);
        ++g_failures;
    }
}

// Relative closeness with a floor, for float-ish comparisons.
bool Close(double a, double b, double tol)
{
    const double d = std::fabs(a - b);
    const double m = std::fmax(1.0, std::fmax(std::fabs(a), std::fabs(b)));
    return d <= tol * m;
}

// 1D target on [0,1]: a broad bump plus a tall narrow spike, so the
// integrand is "spiky" (the regime ERPT is meant to accelerate).
double Target(double x)
{
    if (x < 0.0 || x > 1.0)
        return 0.0;
    const double b1 = std::exp(-((x - 0.30) * (x - 0.30)) / (2.0 * 0.10 * 0.10));
    const double b2 = 6.0 * std::exp(-((x - 0.72) * (x - 0.72)) / (2.0 * 0.02 * 0.02));
    return b1 + b2;
}

// Exact integral by fine Simpson quadrature (reference for checks B and C).
double ExactIntegral(int n = 200000)
{
    if (n % 2)
        ++n;
    const double h = 1.0 / n;
    double s = Target(0.0) + Target(1.0);
    for (int i = 1; i < n; ++i)
        s += Target(i * h) * ((i & 1) ? 4.0 : 2.0);
    return s * h / 3.0;
}

// One Metropolis chain of length m over the 1D target, using the exact
// split-deposit accounting from erpt_proc.cpp. Deposits into `acc` (length
// nBins) and returns the total energy deposited by this chain.
double RunChain(double x0, double depositionEnergy, int m, std::mt19937 &rng,
                std::vector<double> *acc, int nBins, double *outMaxErr)
{
    std::uniform_real_distribution<double> uni(0.0, 1.0);
    const double step = 0.08; // symmetric random-walk proposal width
    double x = x0;
    double gx = Target(x);
    double accumulatedWeight = 0.0;
    double total = 0.0;

    auto deposit = [&](double xx, double energy)
    {
        if (energy <= 0.0)
            return;
        int b = (int)(xx * nBins);
        if (b < 0)
            b = 0;
        if (b >= nBins)
            b = nBins - 1;
        (*acc)[b] += energy;
        total += energy;
    };

    for (int it = 0; it < m; ++it)
    {
        // Symmetric proposal; out-of-range => target 0 => always rejected.
        double xp = x + (uni(rng) * 2.0 - 1.0) * step;
        double gxp = Target(xp);
        double a = 0.0;
        if (gxp > 0.0)
        {
            a = gx > 0.0 ? std::min(1.0, gxp / gx) : 1.0;
        }
        if (a > 1.0)
            a = 1.0;

        accumulatedWeight += 1.0 - a; // Mitsuba: current keeps 1-a

        if (a == 1.0 || uni(rng) < a)
        {
            // Accept: deposit the carried weight at the *current* path,
            // then carry `a` forward on the new path.
            deposit(x, accumulatedWeight * depositionEnergy);
            x = xp;
            gx = gxp;
            accumulatedWeight = a;
        }
        else
        {
            // Reject: deposit the proposal's share at the proposal point.
            if (a > 0.0)
                deposit(xp, a * depositionEnergy);
        }
    }
    // Tail flush of whatever weight is still carried.
    if (accumulatedWeight > 0.0)
        deposit(x, accumulatedWeight * depositionEnergy);

    const double expect = m * depositionEnergy;
    const double err = std::fabs(total - expect);
    if (outMaxErr && err > *outMaxErr)
        *outMaxErr = err;
    return total;
}

// ---- A. Telescoping invariant -----------------------------------------
void TestTelescoping()
{
    std::mt19937 rng(0xC0FFEEu);
    const int nBins = 4; // coarse; we only care about the total here
    std::vector<double> acc(nBins, 0.0);
    double maxRelErr = 0.0;
    const int trials = 200000;
    for (int i = 0; i < trials; ++i)
    {
        const double depE = 1.0 / (3.7 * 8.0); // arbitrary nonzero
        const double x0 = (i % 100) / 100.0;
        RunChain(x0, depE, 8, rng, &acc, nBins, &maxRelErr);
    }
    // Every chain must deposit exactly m*depE; maxRelErr is absolute here
    // since depE*m is O(1). Accumulated float drift over the run is tiny.
    Check(maxRelErr < 1e-9, "telescoping: per-chain total == m*depositionEnergy");
}

// ---- B + C. Stationarity and unbiasedness -----------------------------
void TestStationaryAndUnbiased()
{
    const double I = ExactIntegral();
    std::mt19937 rng(0xBADBEEFu);
    std::uniform_real_distribution<double> uni(0.0, 1.0);

    const int nSeeds = 400000;
    const int m = 8;
    const double numChainsParam = 64.0; // scales how many chains per seed
    const double luminance = I;         // reference scale (paper uses avg lum)

    const int nBins = 100;
    std::vector<double> acc(nBins, 0.0);
    double totalEnergy = 0.0;

    for (int s = 0; s < nSeeds; ++s)
    {
        // Seed from uniform q on [0,1]; unweighted target value is f, so
        // the MIS-free path weight w = f/q = g(xbar).
        const double xbar = uni(rng);
        const double w = Target(xbar); // q == 1 on [0,1]
        if (w <= 0.0)
            continue;

        const double meanChains = numChainsParam * w / luminance;
        const int chains = (int)std::floor(uni(rng) + meanChains);
        if (chains == 0)
            continue;
        const double depE = w / (meanChains * m);

        for (int c = 0; c < chains; ++c)
        {
            std::vector<double> one(nBins, 0.0);
            RunChain(xbar, depE, m, rng, &one, nBins, nullptr);
            for (int b = 0; b < nBins; ++b)
                acc[b] += one[b];
        }
    }
    for (double e : acc)
        totalEnergy += e;

    // C. Unbiasedness: total / seeds -> integral(g).
    const double est = totalEnergy / nSeeds;
    Check(Close(est, I, 0.03), "unbiasedness: total/nSeeds ~= integral(g)");

    // B. Stationarity: normalized deposit histogram ~= g(x)/integral.
    // Compare via the cumulative distribution to be robust to per-bin noise.
    std::vector<double> cdf(nBins + 1, 0.0), ref(nBins + 1, 0.0);
    for (int b = 0; b < nBins; ++b)
        cdf[b + 1] = cdf[b] + acc[b];
    const double scale = cdf[nBins] > 0 ? cdf[nBins] : 1.0;
    for (int b = 0; b <= nBins; ++b)
        cdf[b] /= scale;
    // Reference CDF of g over [0,1].
    const int sub = 200;
    for (int b = 0; b < nBins; ++b)
    {
        double seg = 0.0;
        for (int k = 0; k < sub; ++k)
            seg += Target((b + (k + 0.5) / sub) / nBins);
        ref[b + 1] = ref[b] + seg / sub;
    }
    for (int b = 0; b <= nBins; ++b)
        ref[b] /= ref[nBins];

    double maxCdfErr = 0.0;
    for (int b = 0; b <= nBins; ++b)
        maxCdfErr = std::fmax(maxCdfErr, std::fabs(cdf[b] - ref[b]));
    Check(maxCdfErr < 0.02, "stationarity: deposit CDF matches g(x) CDF");
}
} // namespace

int main()
{
    TestTelescoping();
    TestStationaryAndUnbiased();
    if (g_failures == 0)
        std::printf("erpt_chain_check: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
