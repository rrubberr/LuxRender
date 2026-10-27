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

#include "wf_common.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace lux2::wf;

namespace
{

	// Add packet to sample[rec] for each active lane.
	template <class T, class V, class M>
	inline void AddToSampleScalar(T *sample, const UInt32P &rec, const V &x, const M &m)
	{
		alignas(64) std::uint32_t recV[PACKET_WIDTH];
		alignas(64) T xV[PACKET_WIDTH];
		enoki::store(recV, rec); // must be aligned
		enoki::store(xV, x);
		for (unsigned i = 0; i < PACKET_WIDTH; ++i)
			if (m[i])
				sample[recV[i]] += xV[i];
	}

	template <class T, class V, class M>
	inline void AddToSampleScatter(T *sample, const UInt32P &rec, const V &x, const M &m)
	{
		enoki::scatter_add(sample, x, rec, m);
	}

	enum Form
	{
		Scalar = 0,
		Scatter = 1
	};
	const char *FormName(int f) { return f == Scalar ? "scalar " : "scatter"; }

} // namespace

int main(int argc, char **argv)
{
	const int W = (int)PACKET_WIDTH;
	printf("=== 3: AddToSample scalar loop vs scatter_add (W=%d) ===\n", W);
	printf("AVX512CD available: %s\n",
#if defined(ENOKI_X86_AVX512CD)
		   "yes"
#else
		   "no  (scatter_add falls back to a scalar, the comparison is meaningless)"
#endif
	);
	printf("units: TSC ticks, timer overhead (%.0f) subtracted\n\n", TimerOverhead());

	XorShift rng(0xADD5ULL);
	const int iters = (argc > 1) ? std::atoi(argv[1]) : 500000;
	const int itersCold = iters / 10 > 0 ? iters / 10 : 1;
	const std::uint32_t N = 1u << 20; // 4 MB pool of u32

	const UInt32P x(enoki::arange<UInt32P>() + UInt32P(1u));
	const MaskP mAll(true);

	// ---- timed scenarios ------------------------------------------------
	struct Scenario
	{
		const char *name;
		bool cold;
		int count;
	};
	const Scenario scenarios[] = {
		{"hot (consecutive distinct ids)", false, iters},
		{"cold (random ids, flushed)", true, itersCold},
		{"pairs (W/2 conflicts)", false, iters},
		{"all-same (full conflict chain)", false, iters},
	};
	for (int s = 0; s < 4; ++s)
	{
		printf("--- AddToSample: %s ---\n", scenarios[s].name);
		const int cnt = scenarios[s].count;
		std::vector<std::uint32_t> ids((size_t)cnt * W);
		for (int it = 0; it < cnt; ++it)
			for (int i = 0; i < W; ++i)
			{
				std::uint32_t v;
				switch (s)
				{
				case 0:
					v = 1000u + (std::uint32_t)i;
					break;
				case 1:
					v = rng.NextU32() % N;
					break;
				case 2:
					v = 1000u + (std::uint32_t)(i / 2);
					break;
				default:
					v = 1000u;
					break;
				}
				ids[(size_t)it * W + i] = v;
			}
		for (int f = 0; f < 2; ++f)
		{
			AVec<std::uint32_t> sample(N, 0u);
			UInt32P rec;
			const double cyc = TimeMedian(cnt, [&](int it)
										  {
				    const std::uint32_t *p = &ids[(size_t)it * W];
				    rec = enoki::load_unaligned<UInt32P>(p);
				    if (scenarios[s].cold) {
					    for (int i = 0; i < W; ++i)
						    FlushLine(&sample[p[i]]);
					    FlushFence();
				    } }, [&](int)
										  {
				    if (f == Scalar)
					    AddToSampleScalar(sample.data(), rec, x, mAll);
				    else
					    AddToSampleScatter(sample.data(), rec, x, mAll); }, scenarios[s].cold ? 1 : kHotReps);
			std::uint32_t chk = 0;
			for (int i = 0; i < W; ++i)
				chk += sample[ids[i]];
			DoNotOptimize(chk);
			printf("  [%s] %8.1f cyc\n", FormName(f), cyc);
		}
		printf("\n");
	}

	// Scalar vs scatter_add on conflict heavy patterns.
	printf("--- correctness: scalar vs scatter equivalence ---\n");
	int failures = 0;
	const char *patName[] = {"distinct", "pairs", "all-same", "random in [0,4)", "random in [0,64)",
							 "random over pool"};
	for (int pat = 0; pat < 6; ++pat)
	{
		AVec<std::uint32_t> s1(N, 0u), s2(N, 0u);
		AVec<float> f1(N, 0.f), f2(N, 0.f);
		bool maskedToo = true;
		for (int it = 0; it < 2000; ++it)
		{
			alignas(64) std::uint32_t r[PACKET_WIDTH];
			alignas(64) std::uint32_t xv[PACKET_WIDTH];
			alignas(64) float xf[PACKET_WIDTH];
			alignas(64) std::uint32_t mk[PACKET_WIDTH];
			for (int i = 0; i < W; ++i)
			{
				switch (pat)
				{
				case 0:
					r[i] = 17u + (std::uint32_t)i;
					break;
				case 1:
					r[i] = 17u + (std::uint32_t)(i / 2);
					break;
				case 2:
					r[i] = 17u;
					break;
				case 3:
					r[i] = rng.NextU32() % 4u;
					break;
				case 4:
					r[i] = rng.NextU32() % 64u;
					break;
				default:
					r[i] = rng.NextU32() % N;
					break;
				}
				xv[i] = rng.NextU32() % 1000u;
				xf[i] = rng.NextF();
				mk[i] = (it % 3 == 0) ? 0xFFFFFFFFu : (rng.NextU32() & 1u ? 0xFFFFFFFFu : 0u);
			}
			const UInt32P rec = enoki::load<UInt32P>(r);
			const UInt32P xu = enoki::load<UInt32P>(xv);
			const FloatP xff = enoki::load<FloatP>(xf);
			const MaskP m = enoki::neq(enoki::load<UInt32P>(mk), UInt32P(0u));
			AddToSampleScalar(s1.data(), rec, xu, m);
			AddToSampleScatter(s2.data(), rec, xu, m);
			AddToSampleScalar(f1.data(), rec, xff, m);
			AddToSampleScatter(f2.data(), rec, xff, m);
		}
		(void)maskedToo;
		const bool eqU = (s1 == s2);
		double worst = 0.0;
		for (std::uint32_t i = 0; i < N; ++i)
		{
			const double d = std::fabs((double)f1[i] - (double)f2[i]);
			const double sc = std::fabs((double)f1[i]) + 1e-6;
			if (d / sc > worst)
				worst = d / sc;
		}
		const bool eqF = worst < 1e-4;
		printf("  %-18s uint32 exact: %s   float max rel err: %.2e (%s)\n", patName[pat],
			   eqU ? "YES" : "NO ", worst, eqF ? "ok" : "BAD");
		if (!eqU)
		{
			for (std::uint32_t i = 0; i < N; ++i)
				if (s1[i] != s2[i])
				{
					printf("    first diff at sample %u: scalar=%u scatter=%u\n", i, s1[i], s2[i]);
					break;
				}
		}
		failures += (!eqU) + (!eqF);
	}
	printf("  %s\n", failures ? "FAIL: scatter_add is not equivalent" : "all patterns equivalent");

	printf("\nKeep the chosen form behind one AddToSample function so the swap\n"
		   "is a one-line change. Report cyc for hot/cold above.\n");
	return failures ? 1 : 0;
}
