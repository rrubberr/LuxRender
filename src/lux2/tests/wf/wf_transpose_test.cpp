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

#include "wf_transpose.h"
#include <cstdio>
#include <cstring>
using namespace lux2::wf;

static int Report(const char *name, bool ok)
{
	printf("%-44s %s\n", name, ok ? "OK" : "MISMATCH");
	return ok ? 0 : 1;
}

int main()
{
	int fail = 0;
#if defined(__AVX512F__)
	{
		alignas(64) std::uint32_t in[16][16], out[16][16], ref[16][16];
		for (int r = 0; r < 16; ++r)
			for (int c = 0; c < 16; ++c)
				in[r][c] = (std::uint32_t)(r * 100 + c);
		TransposeScalar(&in[0][0], &ref[0][0], 16, 16);

		std::memset(out, 0, sizeof out);
		Transpose16x16(in, out);
		fail += Report("Transpose16x16 (memory) vs scalar", std::memcmp(out, ref, sizeof out) == 0);

		__m512 r[16], o[16];
		for (int i = 0; i < 16; ++i)
			r[i] = _mm512_castsi512_ps(_mm512_load_si512(&in[i][0]));
		Transpose16x16Reg(r, o);
		std::memset(out, 0, sizeof out);
		for (int i = 0; i < 16; ++i)
			_mm512_store_si512(&out[i][0], _mm512_castps_si512(o[i]));
		fail += Report("Transpose16x16Reg (register) vs scalar", std::memcmp(out, ref, sizeof out) == 0);
	}
	{
		alignas(64) std::uint32_t in8[8][16], out8[8][16], ref[16][8];
		for (int w = 0; w < 8; ++w)
			for (int l = 0; l < 16; ++l)
				in8[w][l] = (std::uint32_t)(w * 100 + l);
		TransposeScalar(&in8[0][0], &ref[0][0], 8, 16); // 8 rows x 16 -> 16 x 8, lane-major
		std::memset(out8, 0xFF, sizeof out8);
		Transpose16x8(in8, out8);
		fail += Report("Transpose16x8: out_flat[lane*8+w] == in[w][lane]", std::memcmp(out8, ref, sizeof ref) == 0);
	}
#else
	printf("AVX-512 kernels skipped (not compiled)\n");
#endif
#if defined(__AVX2__)
	{
		alignas(32) std::uint32_t i88[8][8], o88[8][8], r88[8][8];
		for (int a = 0; a < 8; ++a)
			for (int b = 0; b < 8; ++b)
				i88[a][b] = (std::uint32_t)(a * 10 + b);
		TransposeScalar(&i88[0][0], &r88[0][0], 8, 8);
		Transpose8x8(i88, o88);
		fail += Report("Transpose8x8 vs scalar", std::memcmp(o88, r88, sizeof o88) == 0);
	}
#endif
	printf("%s\n", fail ? "FAILED" : "all transposes correct");
	return fail;
}
