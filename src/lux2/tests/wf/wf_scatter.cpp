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
#include "wf_transpose.h"

#include <cstdint>
#include <cstdio>
#include <vector>

using namespace lux2::wf;

namespace
{
	inline UInt32P Cu(std::uint32_t x) { return UInt32P(x); }
} // namespace

int main(int argc, char **argv)
{
	const int W = (int)PACKET_WIDTH;
	printf("=== 2: scatter vs transpose write/read (W=%d) ===\n", W);
	printf("GEOM_WORDS=%d MIS_WORDS=%d\n", GEOM_WORDS, MIS_WORDS);
	printf("units: TSC ticks, timer overhead (%.0f) subtracted\n\n", TimerOverhead());

	XorShift rng(0xBEEFULL);
	const int itersHot = (argc > 1) ? std::atoi(argv[1]) : 200000;
	const int itersCold = (argc > 2) ? std::atoi(argv[2]) : 20000;

	const std::uint32_t N = 1u << 15; // 32768 records
	const size_t arenaWords = (size_t)N * GEOM_WORDS;
	AVec<std::uint32_t> arena(arenaWords, 0u); // 64B aligned
	printf("arena: %u records x %d words = %.1f MB\n\n", N, GEOM_WORDS, arenaWords * 4 / 1e6);

	// 2a. Scatter a geom record (GEOM_WORDS words) for W lanes.
	printf("--- 2a: scatter geom record (%d words/packet, stores drained) ---\n", GEOM_WORDS);
	for (int hot = 1; hot >= 0; --hot)
	{
		const int iters = hot ? itersHot : itersCold;
		const char *mode = hot ? "hot " : "cold";
		const std::vector<std::uint32_t> sets = MakeIdSets(rng, hot, iters, W, N);
		UInt32P rec, src[GEOM_WORDS];
		const double cyc = TimeMedian(iters, [&](int it)
									  {
			    const std::uint32_t *ids = &sets[(size_t)it * W];
			    rec = enoki::load_unaligned<UInt32P>(ids);
			    for (int w = 0; w < GEOM_WORDS; ++w)
				    src[w] = rec + Cu((std::uint32_t)w);
			    if (!hot) {
				    for (int i = 0; i < W; ++i)
					    for (int h = 0; h < GEOM_WORDS / 16; ++h)
						    FlushLine(&arena[(size_t)ids[i] * GEOM_WORDS + h * 16]);
				    FlushFence();
			    } }, [&](int)
									  {
			    for (int w = 0; w < GEOM_WORDS; ++w)
				    enoki::scatter(&arena[0], src[w], rec * Cu(GEOM_WORDS) + Cu((std::uint32_t)w));
			    DrainStores(); }, hot ? kHotReps : 1);
		const int nAvg = iters < 256 ? iters : 256;
		double lines = 0;
		for (int it = 0; it < nAvg; ++it)
		{
			std::vector<std::uintptr_t> a;
			for (int i = 0; i < W; ++i)
				for (int w = 0; w < GEOM_WORDS; ++w)
					a.push_back((std::uintptr_t)&arena[(size_t)sets[(size_t)it * W + i] * GEOM_WORDS + w]);
			lines += (double)DistinctLines(a);
		}
		printf("  [%s] scatter: %8.1f cyc/packet, %5.1f lines\n", mode, cyc, lines / nAvg);
	}
	printf("\n");

#if defined(__AVX512F__)
	static_assert(LUX2_WF_PACKET_WIDTH == 16, "2/2b assume W=16");
	static_assert(sizeof(UInt32P) == 64, "UInt32P must be one zmm register");

	// 2b. Transpose write of the geom record into a consecutive block.
	printf("--- 2b: transpose write (2x 16x16 + 32 aligned stores) ---\n");

	// tile[w] lane i = word w of record base+i  (value = base + i + w).
	auto BuildTile = [&](std::uint32_t base, UInt32P *tile)
	{
		const UInt32P lane = enoki::arange<UInt32P>() + Cu(base);
		for (int w = 0; w < GEOM_WORDS; ++w)
			tile[w] = lane + Cu((std::uint32_t)w);
	};
	auto ScatterBlock = [&](std::uint32_t base, const UInt32P *tile, std::uint32_t *out)
	{
		const UInt32P recIdx = enoki::arange<UInt32P>() + Cu(base);
		for (int w = 0; w < GEOM_WORDS; ++w)
			enoki::scatter(out, tile[w], recIdx * Cu(GEOM_WORDS) + Cu((std::uint32_t)w));
	};
	auto TransposeBlock = [&](std::uint32_t base, const UInt32P *tile, std::uint32_t *out)
	{
		__m512 inT[16], outT[16];
		for (int half = 0; half < GEOM_WORDS / 16; ++half)
		{
			for (int k = 0; k < 16; ++k)
				inT[k] = _mm512_castsi512_ps(tile[half * 16 + k].m);
			Transpose16x16Reg(inT, outT);
			for (int i = 0; i < W; ++i)
				_mm512_store_si512(&out[(size_t)(base + i) * GEOM_WORDS + half * 16],
								   _mm512_castps_si512(outT[i]));
		}
	};

	// Byte identical check.
	{
		AVec<std::uint32_t> a(arenaWords), b(arenaWords);
		bool allOk = true;
		for (std::uint32_t base : {0u, 5u, 1000u, N - (std::uint32_t)W})
		{
			UInt32P tile[GEOM_WORDS];
			BuildTile(base, tile);
			std::fill(a.begin(), a.end(), 0u);
			std::fill(b.begin(), b.end(), 0u);
			ScatterBlock(base, tile, a.data());
			TransposeBlock(base, tile, b.data());
			bool ok = (a == b);
			for (int i = 0; i < W && ok; ++i)
				for (int w = 0; w < GEOM_WORDS; ++w)
					if (a[(size_t)(base + i) * GEOM_WORDS + w] != base + (std::uint32_t)i + (std::uint32_t)w)
						ok = false;
			allOk = allOk && ok;
		}
		printf("  byte-identical (scatter vs transpose, 4 bases): %s\n\n", allOk ? "YES" : "NO");
		if (!allOk)
			return 1;
	}

	for (int hot = 1; hot >= 0; --hot)
	{
		const int iters = hot ? itersHot : itersCold;
		const char *mode = hot ? "hot " : "cold";
		std::vector<std::uint32_t> bases(iters);
		for (int it = 0; it < iters; ++it)
			bases[it] = hot ? N - (std::uint32_t)W : rng.NextU32() % (N - (std::uint32_t)W + 1);
		UInt32P tile[GEOM_WORDS];
		auto prep = [&](int it)
		{
			BuildTile(bases[it], tile);
			if (!hot)
			{
				for (int i = 0; i < W; ++i)
					for (int h = 0; h < GEOM_WORDS / 16; ++h)
						FlushLine(&arena[(size_t)(bases[it] + i) * GEOM_WORDS + h * 16]);
				FlushFence();
			}
		};
		const double sc = TimeMedian(iters, prep, [&](int it)
									 {
			ScatterBlock(bases[it], tile, arena.data());
			DrainStores(); }, hot ? kHotReps : 1);
		const double tr = TimeMedian(iters, prep, [&](int it)
									 {
			TransposeBlock(bases[it], tile, arena.data());
			DrainStores(); }, hot ? kHotReps : 1);
		std::vector<std::uintptr_t> a;
		for (int i = 0; i < W; ++i)
			for (int w = 0; w < GEOM_WORDS; ++w)
				a.push_back((std::uintptr_t)&arena[(size_t)(bases[0] + i) * GEOM_WORDS + w]);
		printf("  [%s] scatter  : %8.1f cyc/packet, %zu lines\n", mode, sc, DistinctLines(a));
		printf("  [%s] transpose: %8.1f cyc/packet, %zu lines\n", mode, tr, DistinctLines(a));
	}
	printf("\n");

	// 2c. Transpose read load block + transpose vs 32 gathers.
	printf("--- 2c: transpose read vs gather (word-major tile) ---\n");
	for (size_t k = 0; k < arenaWords; ++k)
		arena[k] = rng.NextU32();
	{
		// Both must produce the same 32-register tile.
		bool ok = true;
		for (std::uint32_t base : {0u, 7u, 500u, N - (std::uint32_t)W})
		{
			__m512 outT[GEOM_WORDS];
			for (int half = 0; half < 2; ++half)
			{
				__m512 inT[16];
				for (int i = 0; i < W; ++i)
					inT[i] = _mm512_castsi512_ps(
						_mm512_load_si512(&arena[(size_t)(base + i) * GEOM_WORDS + half * 16]));
				Transpose16x16Reg(inT, outT + half * 16);
			}
			const UInt32P rec = enoki::arange<UInt32P>() + Cu(base);
			for (int w = 0; w < GEOM_WORDS; ++w)
			{
				const UInt32P g = enoki::gather<UInt32P>(arena.data(), rec * Cu(GEOM_WORDS) + Cu((std::uint32_t)w));
				ok = ok && std::memcmp(&g, &outT[w], sizeof(g)) == 0;
			}
		}
		printf("  byte-identical (transpose tile vs gather tile, 4 bases): %s\n", ok ? "YES" : "NO");
		if (!ok)
			return 1;
	}
	for (int hot = 1; hot >= 0; --hot)
	{
		const int iters = hot ? itersHot : itersCold;
		const char *mode = hot ? "hot " : "cold";
		std::vector<std::uint32_t> bases(iters);
		for (int it = 0; it < iters; ++it)
			bases[it] = hot ? 500u : rng.NextU32() % (N - (std::uint32_t)W + 1);
		auto prep = [&](int it)
		{
			if (!hot)
			{
				for (int i = 0; i < W; ++i)
					for (int h = 0; h < GEOM_WORDS / 16; ++h)
						FlushLine(&arena[(size_t)(bases[it] + i) * GEOM_WORDS + h * 16]);
				FlushFence();
			}
		};
		__m512 outT[GEOM_WORDS];
		const double lt = TimeMedian(iters, prep, [&](int it)
									 {
			for (int half = 0; half < 2; ++half) {
				__m512 inT[16];
				for (int i = 0; i < W; ++i)
					inT[i] = _mm512_castsi512_ps(
					    _mm512_load_si512(&arena[(size_t)(bases[it] + i) * GEOM_WORDS + half * 16]));
				Transpose16x16Reg(inT, outT + half * 16);
			}
			for (int w = 0; w < GEOM_WORDS; ++w)
				DoNotOptimize(outT[w]); }, hot ? kHotReps : 1);
		UInt32P tile[GEOM_WORDS];
		const double gt = TimeMedian(iters, prep, [&](int it)
									 {
			const UInt32P rec = enoki::arange<UInt32P>() + Cu(bases[it]);
			for (int w = 0; w < GEOM_WORDS; ++w) {
				tile[w] = enoki::gather<UInt32P>(arena.data(), rec * Cu(GEOM_WORDS) + Cu((std::uint32_t)w));
				DoNotOptimize(tile[w]);
			} }, hot ? kHotReps : 1);
		printf("  [%s] load+transpose: %8.1f cyc\n", mode, lt);
		printf("  [%s] gather x%d    : %8.1f cyc\n", mode, GEOM_WORDS, gt);
	}
	printf("\n");
#else
	printf("--- 2b skipped (needs AVX-512; W=%d here) ---\n\n", W);
#endif

	// 2d. MIS gather from W distinct paths vs contiguous load.
	printf("--- 2d: MIS field gather (5 fields x %d paths) ---\n", W);
	{
		const int misFields = 5;
		std::vector<std::vector<std::uint32_t>> misStore(misFields, std::vector<std::uint32_t>(N));
		for (int f = 0; f < misFields; ++f)
			for (std::uint32_t p = 0; p < N; ++p)
				misStore[f][p] = rng.NextU32();

		// Same path set every iteration = warm (L1/L2); fresh sets + clflush = cold.
		for (int hot = 1; hot >= 0; --hot)
		{
			const int iters = hot ? itersHot : itersCold;
			const char *mode = hot ? "hot " : "cold";
			const std::vector<std::uint32_t> sets = MakeIdSets(rng, hot, iters, W, N);
			AVec<std::uint32_t> packed((size_t)W * misFields);
			UInt32P pathv;
			auto prep = [&](int it)
			{
				const std::uint32_t *paths = &sets[(size_t)it * W];
				pathv = enoki::load_unaligned<UInt32P>(paths);
				for (int f = 0; f < misFields; ++f)
					for (int i = 0; i < W; ++i)
						packed[(size_t)f * W + i] = misStore[f][paths[i]]; // [f][lane]
				if (!hot)
				{
					for (int f = 0; f < misFields; ++f)
						for (int i = 0; i < W; ++i)
							FlushLine(&misStore[f][paths[i]]);
					FlushFence();
				}
			};
			UInt32P g[5];
			const double gc = TimeMedian(iters, prep, [&](int)
										 {
				for (int f = 0; f < misFields; ++f) {
					g[f] = enoki::gather<UInt32P>(misStore[f].data(), pathv);
					DoNotOptimize(g[f]);
				} }, hot ? kHotReps : 1);
			UInt32P l[5];
			const double lc = TimeMedian(iters, prep, [&](int)
										 {
				for (int f = 0; f < misFields; ++f) {
					l[f] = enoki::load<UInt32P>(&packed[(size_t)f * W]);
					DoNotOptimize(l[f]);
				} }, hot ? kHotReps : 1);
			prep(0);
			bool same = true;
			for (int f = 0; f < misFields; ++f)
			{
				const UInt32P a = enoki::gather<UInt32P>(misStore[f].data(), pathv);
				const UInt32P b = enoki::load<UInt32P>(&packed[(size_t)f * W]);
				same = same && std::memcmp(&a, &b, sizeof(a)) == 0;
			}
			printf("  [%s] gather (5 scattered MIS fields): %8.1f cyc\n", mode, gc);
			printf("  [%s] contiguous load (packed)       : %8.1f cyc   (same data: %s)\n", mode, lc,
				   same ? "yes" : "NO");
		}
		printf("  -> MIS loop share of a connector step is measured by the\n"
			   "     occupancy harness; if >20-25%% at depth 48/128, pull Phase 10 forward.\n");
	}

	printf("\nDecision rule: if scatter cyc/packet > 10%% of a walker step,\n"
		   "switch the geom write to the transpose + aligned-store form.\n");
	return 0;
}
