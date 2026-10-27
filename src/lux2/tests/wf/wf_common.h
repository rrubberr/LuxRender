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

#ifndef LUX2_WF_COMMON_H
#define LUX2_WF_COMMON_H

#include <enoki/array.h>
#include <immintrin.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

// The preprocessor cannot see constexpr values.
#if defined(__AVX512F__)
#define LUX2_WF_PACKET_WIDTH 16
#elif defined(__AVX2__)
#define LUX2_WF_PACKET_WIDTH 8
#elif defined(__SSE4_2__)
#define LUX2_WF_PACKET_WIDTH 4
#else
#define LUX2_WF_PACKET_WIDTH 1
#endif

namespace lux2
{
	namespace wf
	{

		// Packet types.
		constexpr size_t PACKET_WIDTH = LUX2_WF_PACKET_WIDTH;

		using FloatP = enoki::Packet<float, PACKET_WIDTH>;
		using Int32P = enoki::Packet<int, PACKET_WIDTH>;
		using UInt32P = enoki::Packet<std::uint32_t, PACKET_WIDTH>;
		using MaskP = enoki::mask_t<FloatP>;

		// Record layout constants.
		constexpr int MAX_DEPTH = 128;
		constexpr int BLOCK_V = 8;
		constexpr int MAX_BLOCKS = MAX_DEPTH / BLOCK_V; // 16
		constexpr int GEOM_WORDS = 32;					// 128B = 2 cachelines
		constexpr int MIS_WORDS = 8;					// 32B
		static_assert(GEOM_WORDS * 4 % 64 == 0, "geom record must be a cacheline multiple");
		static_assert(MIS_WORDS * 4 == 32, "mis record is 32B");

		// 64B aligned std::vector.
		template <class T, size_t Align = 64>
		struct AlignedAllocator
		{
			using value_type = T;
			AlignedAllocator() = default;
			template <class U>
			AlignedAllocator(const AlignedAllocator<U, Align> &) {}
			template <class U>
			struct rebind
			{
				using other = AlignedAllocator<U, Align>;
			};
			T *allocate(size_t n)
			{
				return static_cast<T *>(::operator new(n * sizeof(T), std::align_val_t(Align)));
			}
			void deallocate(T *p, size_t) noexcept { ::operator delete(p, std::align_val_t(Align)); }
			template <class U>
			bool operator==(const AlignedAllocator<U, Align> &) const { return true; }
			template <class U>
			bool operator!=(const AlignedAllocator<U, Align> &) const { return false; }
		};
		template <class T>
		using AVec = std::vector<T, AlignedAllocator<T, 64>>;

		// Timing.
		inline std::uint64_t RDTSC()
		{
#if defined(__x86_64__) || defined(__i386__)
			std::uint32_t lo, hi;
			__asm__ __volatile__("lfence\n\trdtsc\n\tlfence" : "=a"(lo), "=d"(hi) : : "memory");
			return ((std::uint64_t)hi << 32) | lo;
#else
			return 0;
#endif
		}

		inline double MedianTicks(const std::vector<std::uint64_t> &v)
		{
			if (v.empty())
				return 0.0;
			std::vector<std::uint64_t> c(v);
			std::nth_element(c.begin(), c.begin() + c.size() / 2, c.end());
			return (double)c[c.size() / 2];
		}

		// Median cost of an empty RDTSC()..RDTSC().
		inline double TimerOverhead()
		{
			static const double ovh = []
			{
				std::vector<std::uint64_t> v;
				v.reserve(20001);
				for (int i = 0; i < 20001; ++i)
				{
					std::uint64_t a = RDTSC();
					std::uint64_t b = RDTSC();
					v.push_back(b - a);
				}
				return MedianTicks(v);
			}();
			return ovh;
		}

		// Force `v` to be computed before the next volatile asm.
		template <class T>
		inline void DoNotOptimize(T &v)
		{
			__asm__ __volatile__("" : "+m"(v) : : "memory");
		}
		template <class T>
		inline void DoNotOptimize(const T &v)
		{
			__asm__ __volatile__("" : : "m"(v) : "memory");
		}

		// Make the stores of the timed region globally visible before the timer stops.
		inline void DrainStores() { _mm_mfence(); }

		// Evict lines so the next timed access really misses to L3.
		inline void FlushLine(const void *p) { _mm_clflush(p); }
		inline void FlushFence() { _mm_mfence(); }

		// Hot (L1 resident) can be shorter than the timer.
		constexpr int kHotReps = 32;

		// Runs prep(i) untimed, then body(i) `reps` times timed, for i in [0, iters)
		// after a short warm-up. Returns median TSC ticks per body, timer overhead
		// subtracted.
		template <class Prep, class Body>
		double TimeMedian(int iters, Prep &&prep, Body &&body, int reps = 1)
		{
			if (iters <= 0)
				return 0.0;
			if (reps < 1)
				reps = 1;
			const int warm = iters < 64 ? iters : 64;
			for (int i = 0; i < warm; ++i)
			{
				prep(i);
				for (int r = 0; r < reps; ++r)
					body(i);
			}
			std::vector<std::uint64_t> v;
			v.reserve(iters);
			for (int i = 0; i < iters; ++i)
			{
				prep(i);
				std::uint64_t t0 = RDTSC();
				for (int r = 0; r < reps; ++r)
					body(i);
				std::uint64_t t1 = RDTSC();
				v.push_back(t1 - t0);
			}
			const double m = MedianTicks(v), o = TimerOverhead();
			return m > o ? (m - o) / reps : 0.0;
		}

		// Number of distinct 64B cache lines covered by a set of byte addresses.
		inline size_t DistinctLines(const std::vector<std::uintptr_t> &addrs)
		{
			std::vector<std::uint64_t> lines;
			lines.reserve(addrs.size());
			for (std::uintptr_t a : addrs)
				lines.push_back(a >> 6);
			std::sort(lines.begin(), lines.end());
			lines.erase(std::unique(lines.begin(), lines.end()), lines.end());
			return lines.size();
		}

		// Deterministic PRNG
		struct XorShift
		{
			std::uint64_t s0, s1;
			static std::uint64_t SplitMix(std::uint64_t &x)
			{
				x += 0x9E3779B97F4A7C15ULL;
				std::uint64_t z = x;
				z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
				z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
				return z ^ (z >> 31);
			}
			explicit XorShift(std::uint64_t seed)
			{
				std::uint64_t x = seed;
				s0 = SplitMix(x);
				s1 = SplitMix(x);
				if (!(s0 | s1))
					s1 = 1;
			}
			std::uint64_t Next()
			{
				std::uint64_t x = s0;
				const std::uint64_t y = s1;
				s0 = y;
				x ^= x << 23;
				s1 = x ^ y ^ (x >> 17) ^ (y >> 26);
				return s1 + y;
			}
			std::uint32_t NextU32() { return (std::uint32_t)(Next() >> 32); }
			float NextF() { return (float)(NextU32() >> 8) * (1.f / 16777216.f); } // [0,1)
		};

		// `count` sets of W distinct indices in [0, N).
		inline std::vector<std::uint32_t> MakeIdSets(XorShift &rng, bool hot, int count, int W,
													 std::uint32_t N)
		{
			std::vector<std::uint32_t> s((size_t)count * W);
			for (int it = 0; it < count; ++it)
			{
				std::uint32_t *set = &s[(size_t)it * W];
				for (int i = 0; i < W; ++i)
				{
					if (hot)
					{
						set[i] = N - (std::uint32_t)W + (std::uint32_t)i;
					}
					else
					{
						for (;;)
						{
							const std::uint32_t v = rng.NextU32() % N;
							bool dup = false;
							for (int j = 0; j < i; ++j)
								dup |= (set[j] == v);
							if (!dup)
							{
								set[i] = v;
								break;
							}
						}
					}
				}
			}
			return s;
		}

	}
} // namespace lux2::wf

#endif // LUX2_WF_COMMON_H
