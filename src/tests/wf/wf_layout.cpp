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

#include <cstdint>
#include <cstdio>
#include <vector>

using namespace lux2::wf;

int main(int argc, char **argv)
{
	const int W = (int)PACKET_WIDTH;
	printf("=== 1: AoS vs SoA vertex layout (W=%d) ===\n", W);
	printf("GEOM_WORDS=%d MIS_WORDS=%d BLOCK_V=%d\n", GEOM_WORDS, MIS_WORDS, BLOCK_V);
	printf("units: TSC ticks, timer overhead (%.0f) subtracted\n\n", TimerOverhead());

	XorShift rng(0xC0FFEEULL);

	const std::uint32_t N = 1u << 18;		   // 262144 records
	const int FIELDS = GEOM_WORDS + MIS_WORDS; // 40
	const bool padSoA = std::getenv("WF_SOA_NOPAD") == nullptr;
	const size_t soaStride = N + (padSoA ? 16 : 0); // words between field arrays
	printf("arena: %u records, AoS geom %.1f MB + mis %.1f MB, SoA %.1f MB (field stride %zu words%s)\n",
		   N, (double)N * GEOM_WORDS * 4 / 1e6, (double)N * MIS_WORDS * 4 / 1e6,
		   (double)soaStride * FIELDS * 4 / 1e6, soaStride,
		   padSoA ? ", staggered" : ", NOT staggered");

	AVec<std::uint32_t> aosGeom((size_t)N * GEOM_WORDS);
	AVec<std::uint32_t> aosMis((size_t)N * MIS_WORDS);
	AVec<std::uint32_t> soa(soaStride * FIELDS);
	for (std::uint32_t rec = 0; rec < N; ++rec)
	{
		for (int w = 0; w < GEOM_WORDS; ++w)
		{
			const std::uint32_t v = rng.NextU32();
			aosGeom[(size_t)rec * GEOM_WORDS + w] = v;
			soa[(size_t)w * soaStride + rec] = v;
		}
		for (int w = 0; w < MIS_WORDS; ++w)
		{
			const std::uint32_t v = rng.NextU32();
			aosMis[(size_t)rec * MIS_WORDS + w] = v;
			soa[(size_t)(GEOM_WORDS + w) * soaStride + rec] = v;
		}
	}
	std::uint32_t *soaField[FIELDS];
	for (int f = 0; f < FIELDS; ++f)
		soaField[f] = &soa[(size_t)f * soaStride];
	printf("\n");

	const int itersHot = (argc > 1) ? std::atoi(argv[1]) : 200000;
	const int itersCold = (argc > 2) ? std::atoi(argv[2]) : 20000;

	auto Cu = [](std::uint32_t x)
	{ return UInt32P(x); };

	// 1a. endpoint geom load.
	printf("--- 1a: endpoint geom load (gather %d fields x %d lanes) ---\n", GEOM_WORDS, W);
	for (int hot = 1; hot >= 0; --hot)
	{
		const int iters = hot ? itersHot : itersCold;
		const char *mode = hot ? "hot " : "cold";
		const std::vector<std::uint32_t> sets = MakeIdSets(rng, hot, iters, W, N);
		const std::uint32_t *ids = nullptr;
		UInt32P rec;

		auto flushAos = [&](int it)
		{
			ids = &sets[(size_t)it * W];
			rec = enoki::load_unaligned<UInt32P>(ids);
			if (!hot)
			{
				for (int i = 0; i < W; ++i)
					for (int h = 0; h < GEOM_WORDS / 16; ++h)
						FlushLine(&aosGeom[(size_t)ids[i] * GEOM_WORDS + h * 16]);
				FlushFence();
			}
		};
		const double aosCyc = TimeMedian(iters, flushAos, [&](int)
										 {
			UInt32P acc(0u);
			for (int w = 0; w < GEOM_WORDS; ++w)
				acc += enoki::gather<UInt32P>(&aosGeom[0], rec * Cu(GEOM_WORDS) + Cu((std::uint32_t)w));
			DoNotOptimize(acc); }, hot ? kHotReps : 1);

		auto flushSoa = [&](int it)
		{
			ids = &sets[(size_t)it * W];
			rec = enoki::load_unaligned<UInt32P>(ids);
			if (!hot)
			{
				for (int i = 0; i < W; ++i)
					for (int w = 0; w < GEOM_WORDS; ++w)
						FlushLine(&soaField[w][ids[i]]);
				FlushFence();
			}
		};
		const double soaCyc = TimeMedian(iters, flushSoa, [&](int)
										 {
			UInt32P acc(0u);
			for (int w = 0; w < GEOM_WORDS; ++w)
				acc += enoki::gather<UInt32P>(soaField[w], rec);
			DoNotOptimize(acc); }, hot ? kHotReps : 1);

		// Analytic distinct lines, averaged over id sets.
		const int nAvg = iters < 256 ? iters : 256;
		double aosL = 0, soaL = 0;
		for (int it = 0; it < nAvg; ++it)
		{
			std::vector<std::uintptr_t> a, s;
			for (int i = 0; i < W; ++i)
			{
				const std::uintptr_t r = sets[(size_t)it * W + i];
				for (int w = 0; w < GEOM_WORDS; ++w)
				{
					a.push_back((std::uintptr_t)&aosGeom[r * GEOM_WORDS + w]);
					s.push_back((std::uintptr_t)&soaField[w][r]);
				}
			}
			aosL += (double)DistinctLines(a);
			soaL += (double)DistinctLines(s);
		}
		printf("  [%s] AoS: %8.1f cyc/packet, %6.1f lines\n", mode, aosCyc, aosL / nAvg);
		printf("  [%s] SoA: %8.1f cyc/packet, %6.1f lines\n", mode, soaCyc, soaL / nAvg);
	}
	printf("\n");

	// 1b. walker write.
	printf("--- 1b: walker write (geom %d + MIS %d = %d words/packet, stores drained) ---\n",
		   GEOM_WORDS, MIS_WORDS, FIELDS);
	for (int hot = 1; hot >= 0; --hot)
	{
		const int iters = hot ? itersHot : itersCold;
		const char *mode = hot ? "hot " : "cold";
		const std::vector<std::uint32_t> sets = MakeIdSets(rng, hot, iters, W, N);
		const std::uint32_t *ids = nullptr;
		UInt32P rec, src[FIELDS];

		auto prep = [&](bool aos)
		{
			return [&, aos](int it)
			{
				ids = &sets[(size_t)it * W];
				rec = enoki::load_unaligned<UInt32P>(ids);
				for (int w = 0; w < FIELDS; ++w)
					src[w] = rec + Cu((std::uint32_t)w);
				if (!hot)
				{
					for (int i = 0; i < W; ++i)
					{
						if (aos)
						{
							for (int h = 0; h < GEOM_WORDS / 16; ++h)
								FlushLine(&aosGeom[(size_t)ids[i] * GEOM_WORDS + h * 16]);
							FlushLine(&aosMis[(size_t)ids[i] * MIS_WORDS]);
						}
						else
						{
							for (int w = 0; w < FIELDS; ++w)
								FlushLine(&soaField[w][ids[i]]);
						}
					}
					FlushFence();
				}
			};
		};
		const double aosCyc = TimeMedian(iters, prep(true), [&](int)
										 {
			for (int w = 0; w < GEOM_WORDS; ++w)
				enoki::scatter(&aosGeom[0], src[w], rec * Cu(GEOM_WORDS) + Cu((std::uint32_t)w));
			for (int w = 0; w < MIS_WORDS; ++w)
				enoki::scatter(&aosMis[0], src[GEOM_WORDS + w], rec * Cu(MIS_WORDS) + Cu((std::uint32_t)w));
			DrainStores(); }, hot ? kHotReps : 1);
		const double soaCyc = TimeMedian(iters, prep(false), [&](int)
										 {
			for (int w = 0; w < FIELDS; ++w)
				enoki::scatter(soaField[w], src[w], rec);
			DrainStores(); }, hot ? kHotReps : 1);

		const int nAvg = iters < 256 ? iters : 256;
		double aosL = 0, soaL = 0;
		for (int it = 0; it < nAvg; ++it)
		{
			std::vector<std::uintptr_t> a, s;
			for (int i = 0; i < W; ++i)
			{
				const std::uintptr_t r = sets[(size_t)it * W + i];
				for (int w = 0; w < GEOM_WORDS; ++w)
					a.push_back((std::uintptr_t)&aosGeom[r * GEOM_WORDS + w]);
				for (int w = 0; w < MIS_WORDS; ++w)
					a.push_back((std::uintptr_t)&aosMis[r * MIS_WORDS + w]);
				for (int w = 0; w < FIELDS; ++w)
					s.push_back((std::uintptr_t)&soaField[w][r]);
			}
			aosL += (double)DistinctLines(a);
			soaL += (double)DistinctLines(s);
		}
		printf("  [%s] AoS: %8.1f cyc/packet, %6.1f lines\n", mode, aosCyc, aosL / nAvg);
		printf("  [%s] SoA: %8.1f cyc/packet, %6.1f lines\n", mode, soaCyc, soaL / nAvg);
	}
	printf("\n");

	// 1c. MIS loop.
	printf("--- 1c: MIS loop (5 fields x path length n, %d distinct paths) ---\n", W);
	const int misFields = 5;
	const std::uint32_t pathSlots = N / 128;
	std::uint32_t base[PACKET_WIDTH];
	for (int i = 0; i < W; ++i)
	{
		for (;;)
		{
			const std::uint32_t b = (rng.NextU32() % (pathSlots - 1)) * 128u;
			bool dup = false;
			for (int j = 0; j < i; ++j)
				dup |= (base[j] == b);
			if (!dup)
			{
				base[i] = b;
				break;
			}
		}
	}
	const UInt32P baseV = enoki::load_unaligned<UInt32P>(base);
	for (int n : {8, 16, 48, 128})
	{
		const int iters = itersHot;
		const double aosCyc = TimeMedian(iters, [](int) {}, [&](int)
										 {
			UInt32P acc(0u);
			for (int v = 0; v < n; ++v) {
				const UInt32P idx0 = (baseV + Cu((std::uint32_t)v)) * Cu(MIS_WORDS);
				for (int f = 0; f < misFields; ++f)
					acc += enoki::gather<UInt32P>(&aosMis[0], idx0 + Cu((std::uint32_t)f));
			}
			DoNotOptimize(acc); });
		const double soaCyc = TimeMedian(iters, [](int) {}, [&](int)
										 {
			UInt32P acc(0u);
			for (int f = 0; f < misFields; ++f)
				for (int v = 0; v < n; ++v)
					acc += enoki::gather<UInt32P>(soaField[GEOM_WORDS + f], baseV + Cu((std::uint32_t)v));
			DoNotOptimize(acc); });

		std::vector<std::uintptr_t> a, s;
		for (int i = 0; i < W; ++i)
			for (int v = 0; v < n; ++v)
			{
				for (int k = 0; k < misFields; ++k)
				{
					a.push_back((std::uintptr_t)&aosMis[(size_t)(base[i] + v) * MIS_WORDS + k]);
					s.push_back((std::uintptr_t)&soaField[GEOM_WORDS + k][base[i] + v]);
				}
			}
		printf("  n=%3d: AoS %7.1f cyc, %5.1f lines/lane (ceil(n/2)=%d); "
			   "SoA %7.1f cyc, %5.1f lines/lane (5*ceil(n/16)=%d)\n",
			   n, aosCyc, (double)DistinctLines(a) / W, (n + 1) / 2,
			   soaCyc, (double)DistinctLines(s) / W, misFields * ((n + 15) / 16));
	}

	printf("\nDecision rule: keep AoS unless SoA endpoint-load/write cost is\n"
		   "materially lower on the target. Report cyc/packet above.\n");
	return 0;
}
