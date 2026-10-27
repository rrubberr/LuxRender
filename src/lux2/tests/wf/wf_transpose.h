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

#ifndef LUX2_WF_TRANSPOSE_H
#define LUX2_WF_TRANSPOSE_H

#include <immintrin.h>
#include <cstdint>

namespace lux2
{
	namespace wf
	{

		// Scalar reference transpose.
		inline void TransposeScalar(const std::uint32_t *src, std::uint32_t *dst,
									int rows, int cols)
		{
			for (int r = 0; r < rows; ++r)
				for (int c = 0; c < cols; ++c)
					dst[c * rows + r] = src[r * cols + c];
		}

#if defined(__AVX512F__)

		// In register 16x16 shuffle.
		inline void Transpose16x16Shuffle(const __m512 in[16], __m512 out[16])
		{
			__m512 r0 = in[0], r1 = in[1], r2 = in[2], r3 = in[3];
			__m512 r4 = in[4], r5 = in[5], r6 = in[6], r7 = in[7];
			__m512 r8 = in[8], r9 = in[9], ra = in[10], rb = in[11];
			__m512 rc = in[12], rd = in[13], re = in[14], rf = in[15];

			__m512 t0 = _mm512_unpacklo_ps(r0, r1);
			__m512 t1 = _mm512_unpackhi_ps(r0, r1);
			__m512 t2 = _mm512_unpacklo_ps(r2, r3);
			__m512 t3 = _mm512_unpackhi_ps(r2, r3);
			__m512 t4 = _mm512_unpacklo_ps(r4, r5);
			__m512 t5 = _mm512_unpackhi_ps(r4, r5);
			__m512 t6 = _mm512_unpacklo_ps(r6, r7);
			__m512 t7 = _mm512_unpackhi_ps(r6, r7);
			__m512 t8 = _mm512_unpacklo_ps(r8, r9);
			__m512 t9 = _mm512_unpackhi_ps(r8, r9);
			__m512 ta = _mm512_unpacklo_ps(ra, rb);
			__m512 tb = _mm512_unpackhi_ps(ra, rb);
			__m512 tc = _mm512_unpacklo_ps(rc, rd);
			__m512 td = _mm512_unpackhi_ps(rc, rd);
			__m512 te = _mm512_unpacklo_ps(re, rf);
			__m512 tf = _mm512_unpackhi_ps(re, rf);

			__m512 g = _mm512_shuffle_ps(t0, t2, _MM_SHUFFLE(1, 0, 1, 0));
			__m512 h = _mm512_shuffle_ps(t0, t2, _MM_SHUFFLE(3, 2, 3, 2));
			__m512 i = _mm512_shuffle_ps(t1, t3, _MM_SHUFFLE(1, 0, 1, 0));
			__m512 j = _mm512_shuffle_ps(t1, t3, _MM_SHUFFLE(3, 2, 3, 2));
			__m512 k = _mm512_shuffle_ps(t4, t6, _MM_SHUFFLE(1, 0, 1, 0));
			__m512 l = _mm512_shuffle_ps(t4, t6, _MM_SHUFFLE(3, 2, 3, 2));
			__m512 m = _mm512_shuffle_ps(t5, t7, _MM_SHUFFLE(1, 0, 1, 0));
			__m512 n = _mm512_shuffle_ps(t5, t7, _MM_SHUFFLE(3, 2, 3, 2));
			__m512 o = _mm512_shuffle_ps(t8, ta, _MM_SHUFFLE(1, 0, 1, 0));
			__m512 p = _mm512_shuffle_ps(t8, ta, _MM_SHUFFLE(3, 2, 3, 2));
			__m512 q = _mm512_shuffle_ps(t9, tb, _MM_SHUFFLE(1, 0, 1, 0));
			__m512 rr_ = _mm512_shuffle_ps(t9, tb, _MM_SHUFFLE(3, 2, 3, 2));
			__m512 s = _mm512_shuffle_ps(tc, te, _MM_SHUFFLE(1, 0, 1, 0));
			__m512 tt = _mm512_shuffle_ps(tc, te, _MM_SHUFFLE(3, 2, 3, 2));
			__m512 u = _mm512_shuffle_ps(td, tf, _MM_SHUFFLE(1, 0, 1, 0));
			__m512 v = _mm512_shuffle_ps(td, tf, _MM_SHUFFLE(3, 2, 3, 2));

			t0 = _mm512_shuffle_f32x4(g, k, _MM_SHUFFLE(2, 0, 2, 0));
			t1 = _mm512_shuffle_f32x4(o, s, _MM_SHUFFLE(2, 0, 2, 0));
			t2 = _mm512_shuffle_f32x4(h, l, _MM_SHUFFLE(2, 0, 2, 0));
			t3 = _mm512_shuffle_f32x4(p, tt, _MM_SHUFFLE(2, 0, 2, 0));
			t4 = _mm512_shuffle_f32x4(i, m, _MM_SHUFFLE(2, 0, 2, 0));
			t5 = _mm512_shuffle_f32x4(q, u, _MM_SHUFFLE(2, 0, 2, 0));
			t6 = _mm512_shuffle_f32x4(j, n, _MM_SHUFFLE(2, 0, 2, 0));
			t7 = _mm512_shuffle_f32x4(rr_, v, _MM_SHUFFLE(2, 0, 2, 0));
			t8 = _mm512_shuffle_f32x4(g, k, _MM_SHUFFLE(3, 1, 3, 1));
			t9 = _mm512_shuffle_f32x4(o, s, _MM_SHUFFLE(3, 1, 3, 1));
			ta = _mm512_shuffle_f32x4(h, l, _MM_SHUFFLE(3, 1, 3, 1));
			tb = _mm512_shuffle_f32x4(p, tt, _MM_SHUFFLE(3, 1, 3, 1));
			tc = _mm512_shuffle_f32x4(i, m, _MM_SHUFFLE(3, 1, 3, 1));
			td = _mm512_shuffle_f32x4(q, u, _MM_SHUFFLE(3, 1, 3, 1));
			te = _mm512_shuffle_f32x4(j, n, _MM_SHUFFLE(3, 1, 3, 1));
			tf = _mm512_shuffle_f32x4(rr_, v, _MM_SHUFFLE(3, 1, 3, 1));

			r0 = _mm512_shuffle_f32x4(t0, t1, _MM_SHUFFLE(2, 0, 2, 0));
			r1 = _mm512_shuffle_f32x4(t2, t3, _MM_SHUFFLE(2, 0, 2, 0));
			r2 = _mm512_shuffle_f32x4(t4, t5, _MM_SHUFFLE(2, 0, 2, 0));
			r3 = _mm512_shuffle_f32x4(t6, t7, _MM_SHUFFLE(2, 0, 2, 0));
			r4 = _mm512_shuffle_f32x4(t8, t9, _MM_SHUFFLE(2, 0, 2, 0));
			r5 = _mm512_shuffle_f32x4(ta, tb, _MM_SHUFFLE(2, 0, 2, 0));
			r6 = _mm512_shuffle_f32x4(tc, td, _MM_SHUFFLE(2, 0, 2, 0));
			r7 = _mm512_shuffle_f32x4(te, tf, _MM_SHUFFLE(2, 0, 2, 0));
			r8 = _mm512_shuffle_f32x4(t0, t1, _MM_SHUFFLE(3, 1, 3, 1));
			r9 = _mm512_shuffle_f32x4(t2, t3, _MM_SHUFFLE(3, 1, 3, 1));
			ra = _mm512_shuffle_f32x4(t4, t5, _MM_SHUFFLE(3, 1, 3, 1));
			rb = _mm512_shuffle_f32x4(t6, t7, _MM_SHUFFLE(3, 1, 3, 1));
			rc = _mm512_shuffle_f32x4(t8, t9, _MM_SHUFFLE(3, 1, 3, 1));
			rd = _mm512_shuffle_f32x4(ta, tb, _MM_SHUFFLE(3, 1, 3, 1));
			re = _mm512_shuffle_f32x4(tc, td, _MM_SHUFFLE(3, 1, 3, 1));
			rf = _mm512_shuffle_f32x4(te, tf, _MM_SHUFFLE(3, 1, 3, 1));

			out[0] = r0;
			out[1] = r1;
			out[2] = r2;
			out[3] = r3;
			out[4] = r4;
			out[5] = r5;
			out[6] = r6;
			out[7] = r7;
			out[8] = r8;
			out[9] = r9;
			out[10] = ra;
			out[11] = rb;
			out[12] = rc;
			out[13] = rd;
			out[14] = re;
			out[15] = rf;
		}

		// Register form: in[16] packets -> out[16] packets.
		inline void Transpose16x16Reg(const __m512 in[16], __m512 out[16])
		{
			Transpose16x16Shuffle(in, out);
		}

		// Memory form: uint32 in[16][16] row-major -> out[16][16].
		inline void Transpose16x16(const std::uint32_t in[16][16], std::uint32_t out[16][16])
		{
			__m512 r[16], o[16];
			for (int i = 0; i < 16; ++i)
				r[i] = _mm512_castsi512_ps(_mm512_load_si512((const __m512i *)&in[i][0]));
			Transpose16x16Shuffle(r, o);
			for (int i = 0; i < 16; ++i)
				_mm512_store_si512((__m512i *)&out[i][0], _mm512_castps_si512(o[i]));
		}

		// 8 word block reorganization for MIS.
		inline void Transpose16x8(const std::uint32_t in[8][16], std::uint32_t out[8][16])
		{
			__m512 r0 = _mm512_castsi512_ps(_mm512_load_si512((const __m512i *)&in[0][0]));
			__m512 r1 = _mm512_castsi512_ps(_mm512_load_si512((const __m512i *)&in[1][0]));
			__m512 r2 = _mm512_castsi512_ps(_mm512_load_si512((const __m512i *)&in[2][0]));
			__m512 r3 = _mm512_castsi512_ps(_mm512_load_si512((const __m512i *)&in[3][0]));
			__m512 r4 = _mm512_castsi512_ps(_mm512_load_si512((const __m512i *)&in[4][0]));
			__m512 r5 = _mm512_castsi512_ps(_mm512_load_si512((const __m512i *)&in[5][0]));
			__m512 r6 = _mm512_castsi512_ps(_mm512_load_si512((const __m512i *)&in[6][0]));
			__m512 r7 = _mm512_castsi512_ps(_mm512_load_si512((const __m512i *)&in[7][0]));

			__m512 t0 = _mm512_unpacklo_ps(r0, r1);
			__m512 t1 = _mm512_unpackhi_ps(r0, r1);
			__m512 t2 = _mm512_unpacklo_ps(r2, r3);
			__m512 t3 = _mm512_unpackhi_ps(r2, r3);
			__m512 t4 = _mm512_unpacklo_ps(r4, r5);
			__m512 t5 = _mm512_unpackhi_ps(r4, r5);
			__m512 t6 = _mm512_unpacklo_ps(r6, r7);
			__m512 t7 = _mm512_unpackhi_ps(r6, r7);

			__m512 t8 = _mm512_shuffle_ps(t0, t2, _MM_SHUFFLE(1, 0, 1, 0));
			__m512 t9 = _mm512_shuffle_ps(t0, t2, _MM_SHUFFLE(3, 2, 3, 2));
			__m512 ta = _mm512_shuffle_ps(t1, t3, _MM_SHUFFLE(1, 0, 1, 0));
			__m512 tb = _mm512_shuffle_ps(t1, t3, _MM_SHUFFLE(3, 2, 3, 2));
			__m512 tc = _mm512_shuffle_ps(t4, t6, _MM_SHUFFLE(1, 0, 1, 0));
			__m512 td = _mm512_shuffle_ps(t4, t6, _MM_SHUFFLE(3, 2, 3, 2));
			__m512 te = _mm512_shuffle_ps(t5, t7, _MM_SHUFFLE(1, 0, 1, 0));
			__m512 tf = _mm512_shuffle_ps(t5, t7, _MM_SHUFFLE(3, 2, 3, 2));

			t0 = _mm512_shuffle_f32x4(t8, tc, _MM_SHUFFLE(2, 0, 2, 0));
			t1 = _mm512_shuffle_f32x4(t9, td, _MM_SHUFFLE(2, 0, 2, 0));
			t2 = _mm512_shuffle_f32x4(ta, te, _MM_SHUFFLE(2, 0, 2, 0));
			t3 = _mm512_shuffle_f32x4(tb, tf, _MM_SHUFFLE(2, 0, 2, 0));
			t4 = _mm512_shuffle_f32x4(t8, tc, _MM_SHUFFLE(3, 1, 3, 1));
			t5 = _mm512_shuffle_f32x4(t9, td, _MM_SHUFFLE(3, 1, 3, 1));
			t6 = _mm512_shuffle_f32x4(ta, te, _MM_SHUFFLE(3, 1, 3, 1));
			t7 = _mm512_shuffle_f32x4(tb, tf, _MM_SHUFFLE(3, 1, 3, 1));

			r0 = _mm512_shuffle_f32x4(t0, t1, _MM_SHUFFLE(2, 0, 2, 0));
			r1 = _mm512_shuffle_f32x4(t2, t3, _MM_SHUFFLE(2, 0, 2, 0));
			r2 = _mm512_shuffle_f32x4(t4, t5, _MM_SHUFFLE(2, 0, 2, 0));
			r3 = _mm512_shuffle_f32x4(t6, t7, _MM_SHUFFLE(2, 0, 2, 0));
			r4 = _mm512_shuffle_f32x4(t0, t1, _MM_SHUFFLE(3, 1, 3, 1));
			r5 = _mm512_shuffle_f32x4(t2, t3, _MM_SHUFFLE(3, 1, 3, 1));
			r6 = _mm512_shuffle_f32x4(t4, t5, _MM_SHUFFLE(3, 1, 3, 1));
			r7 = _mm512_shuffle_f32x4(t6, t7, _MM_SHUFFLE(3, 1, 3, 1));

			_mm512_store_si512((__m512i *)&out[0][0], _mm512_castps_si512(r0));
			_mm512_store_si512((__m512i *)&out[1][0], _mm512_castps_si512(r1));
			_mm512_store_si512((__m512i *)&out[2][0], _mm512_castps_si512(r2));
			_mm512_store_si512((__m512i *)&out[3][0], _mm512_castps_si512(r3));
			_mm512_store_si512((__m512i *)&out[4][0], _mm512_castps_si512(r4));
			_mm512_store_si512((__m512i *)&out[5][0], _mm512_castps_si512(r5));
			_mm512_store_si512((__m512i *)&out[6][0], _mm512_castps_si512(r6));
			_mm512_store_si512((__m512i *)&out[7][0], _mm512_castps_si512(r7));
		}
#endif // __AVX512F__

#if defined(__AVX2__)
		// 8x8 uint32 transpose.
		inline void Transpose8x8(const std::uint32_t in[8][8], std::uint32_t out[8][8])
		{
			__m256 r0 = _mm256_castsi256_ps(_mm256_load_si256((const __m256i *)&in[0][0]));
			__m256 r1 = _mm256_castsi256_ps(_mm256_load_si256((const __m256i *)&in[1][0]));
			__m256 r2 = _mm256_castsi256_ps(_mm256_load_si256((const __m256i *)&in[2][0]));
			__m256 r3 = _mm256_castsi256_ps(_mm256_load_si256((const __m256i *)&in[3][0]));
			__m256 r4 = _mm256_castsi256_ps(_mm256_load_si256((const __m256i *)&in[4][0]));
			__m256 r5 = _mm256_castsi256_ps(_mm256_load_si256((const __m256i *)&in[5][0]));
			__m256 r6 = _mm256_castsi256_ps(_mm256_load_si256((const __m256i *)&in[6][0]));
			__m256 r7 = _mm256_castsi256_ps(_mm256_load_si256((const __m256i *)&in[7][0]));

			__m256 t0 = _mm256_unpacklo_ps(r0, r1);
			__m256 t1 = _mm256_unpackhi_ps(r0, r1);
			__m256 t2 = _mm256_unpacklo_ps(r2, r3);
			__m256 t3 = _mm256_unpackhi_ps(r2, r3);
			__m256 t4 = _mm256_unpacklo_ps(r4, r5);
			__m256 t5 = _mm256_unpackhi_ps(r4, r5);
			__m256 t6 = _mm256_unpacklo_ps(r6, r7);
			__m256 t7 = _mm256_unpackhi_ps(r6, r7);

			__m256 t8 = _mm256_shuffle_ps(t0, t2, _MM_SHUFFLE(1, 0, 1, 0));
			__m256 t9 = _mm256_shuffle_ps(t0, t2, _MM_SHUFFLE(3, 2, 3, 2));
			__m256 ta = _mm256_shuffle_ps(t1, t3, _MM_SHUFFLE(1, 0, 1, 0));
			__m256 tb = _mm256_shuffle_ps(t1, t3, _MM_SHUFFLE(3, 2, 3, 2));
			__m256 tc = _mm256_shuffle_ps(t4, t6, _MM_SHUFFLE(1, 0, 1, 0));
			__m256 td = _mm256_shuffle_ps(t4, t6, _MM_SHUFFLE(3, 2, 3, 2));
			__m256 te = _mm256_shuffle_ps(t5, t7, _MM_SHUFFLE(1, 0, 1, 0));
			__m256 tf = _mm256_shuffle_ps(t5, t7, _MM_SHUFFLE(3, 2, 3, 2));

			r0 = _mm256_permute2f128_ps(t8, tc, _MM_SHUFFLE(0, 2, 0, 0));
			r1 = _mm256_permute2f128_ps(t9, td, _MM_SHUFFLE(0, 2, 0, 0));
			r2 = _mm256_permute2f128_ps(ta, te, _MM_SHUFFLE(0, 2, 0, 0));
			r3 = _mm256_permute2f128_ps(tb, tf, _MM_SHUFFLE(0, 2, 0, 0));
			r4 = _mm256_permute2f128_ps(t8, tc, _MM_SHUFFLE(0, 3, 0, 1));
			r5 = _mm256_permute2f128_ps(t9, td, _MM_SHUFFLE(0, 3, 0, 1));
			r6 = _mm256_permute2f128_ps(ta, te, _MM_SHUFFLE(0, 3, 0, 1));
			r7 = _mm256_permute2f128_ps(tb, tf, _MM_SHUFFLE(0, 3, 0, 1));

			_mm256_store_si256((__m256i *)&out[0][0], _mm256_castps_si256(r0));
			_mm256_store_si256((__m256i *)&out[1][0], _mm256_castps_si256(r1));
			_mm256_store_si256((__m256i *)&out[2][0], _mm256_castps_si256(r2));
			_mm256_store_si256((__m256i *)&out[3][0], _mm256_castps_si256(r3));
			_mm256_store_si256((__m256i *)&out[4][0], _mm256_castps_si256(r4));
			_mm256_store_si256((__m256i *)&out[5][0], _mm256_castps_si256(r5));
			_mm256_store_si256((__m256i *)&out[6][0], _mm256_castps_si256(r6));
			_mm256_store_si256((__m256i *)&out[7][0], _mm256_castps_si256(r7));
		}
#endif // __AVX2__

	}
} // namespace lux2::wf

#endif // LUX2_WF_TRANSPOSE_H
