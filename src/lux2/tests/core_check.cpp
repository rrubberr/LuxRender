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

// Tests the lux2 core types. Exercises (vecp, geometry, spectrum, ray, rng).
// Built and run as the lux2corecheck target / ctest.

#include "core/lux2.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

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

// True if every lane of 'a' is within 'eps' of the scalar 'b'.
bool AllClose(const FloatP &a, float b, float eps = 1e-4f) {
	return enoki::all(enoki::abs(a - FloatP(b)) < FloatP(eps));
}

// vecp.h: packet width and basic arithmetic.
void CheckVecP() {
	std::cout << "[vecp] PACKET_WIDTH=" << PACKET_WIDTH << std::endl;
	Check(PACKET_WIDTH >= 1 && PACKET_WIDTH <= 16, "packet width in range");

	const FloatP x = static_cast<float>(std::rand() % 100 + 1);
	const FloatP y = enoki::fmadd(x, x, FloatP(1.f));
	Check(enoki::hsum(y) > 0.f, "fmadd + hsum");

	const Int32P i(7);
	Check(enoki::hsum(i * i) == float(49 * int(PACKET_WIDTH)), "int32 packet mul");
}

// geometry.h: Point/Vector/Normal, cross/dot, coordinate_system.
void CheckGeometry() {
	// A runtime-varying direction.
	const float a = float(std::rand() % 100 + 1) / 100.f;
	Vector3fP v(FloatP(a), FloatP(1.f), FloatP(-a));
	const FloatP n = enoki::norm(v);
	// norm is identical across lanes.
	Check(enoki::all(enoki::abs(n - enoki::hsum(n) / float(PACKET_WIDTH)) < FloatP(1e-3f)),
		"vector norm uniform across lanes");

	// cross(x, y) == z
	Vector3fP x(1.f, 0.f, 0.f), y(0.f, 1.f, 0.f);
	Vector3fP z = enoki::cross(x, y);
	Check(AllClose(z.z(), 1.f), "cross(x,y).z == 1");

	// Point - Point == Vector; Point + Vector == Point.
	Point3fP p(FloatP(a), FloatP(2.f), FloatP(3.f));
	Point3fP q(FloatP(a), FloatP(2.f), FloatP(3.f));
	Vector3fP d = p - q;
	Check(enoki::all(d.x() == FloatP(0.f)) && enoki::all(d.y() == FloatP(0.f)),
		"point-point == zero vector");
	Point3fP r = p + Vector3fP(1.f, 0.f, 0.f);
	Check(AllClose(r.x(), a + 1.f), "point+vector.x");

	// coordinate_system yields an orthonormal basis.
	Normal3fP nrm(FloatP(0.3f), FloatP(a), FloatP(0.7f));
	nrm = enoki::normalize(nrm);
	auto basis = coordinate_system(Vector3fP(nrm));
	const FloatP dotbc = enoki::dot(basis.first, basis.second);
	// Single-precision Duff basis: orthogonality to ~1e-3.
	Check(enoki::all(enoki::abs(dotbc) < FloatP(1e-2f)),
		"coord system basis orthogonal");
}

// spectrum.h: SWCSpectrumP arithmetic and reductions.
void CheckSpectrum() {
	const float a = float(std::rand() % 50 + 1) / 50.f;
	SWCSpectrumP s{FloatP(a)};              // brace-init avoids the vexing parse
	SWCSpectrumP t = s * s;                 // a^2 per sample
	SWCSpectrumP u = s + t;                 // a + a^2
	Check(AllClose(u.c[0], a + a * a), "spectrum mul/add");

	Check(AllClose(s.MaxComponent(), a), "spectrum MaxComponent");
	Check(AllClose(s.Average(), a), "spectrum Average");

	SWCSpectrumP black{0.f};
	Check(enoki::all(black.IsBlack()), "black spectrum IsBlack");
	Check(!enoki::all(s.IsBlack()), "non-black spectrum !IsBlack");

	SWCSpectrumP clamped = (s * -1.f).Clamped();
	Check(enoki::all(clamped.c[0] == FloatP(0.f)), "Clamped to zero");

	// select + fmadd free functions.
	const MaskP m = s.c[0] > FloatP(a * 0.5f);
	SWCSpectrumP sel = lux2::select(m, s, black);
	Check(AllClose(sel.c[0], a), "spectrum select");
	SWCSpectrumP f = lux2::fmadd(s, s, s);  // a^2 + a
	Check(AllClose(f.c[0], a * a + a), "spectrum fmadd");
}

// ray.h: RayP construction, d_rcp, operator(), payload; HitP validity.
void CheckRay() {
	const float a = float(std::rand() % 10 + 1) / 10.f;
	Point3fP o(FloatP(a), FloatP(0.f), FloatP(0.f));
	Vector3fP d(FloatP(0.f), FloatP(1.f), FloatP(0.f));
	RayP ray(o, d, FloatP(0.f),
		FloatP(std::numeric_limits<float>::infinity()), FloatP(0.f));
	ray.InitPayload();

	// d_rcp.y should be 1 (reciprocal of 1).
	Check(AllClose(ray.d_rcp.y(), 1.f), "d_rcp computed");

	// ray(2) == o + d*2 -> (a, 2, 0)
	Point3fP pos = ray(FloatP(2.f));
	Check(AllClose(pos.x(), a), "ray(t).x");
	Check(AllClose(pos.y(), 2.f), "ray(t).y");

	// Payload initialized.
	Check(enoki::all(ray.throughput.c[0] == FloatP(1.f)), "throughput init");
	Check(enoki::all(ray.alive), "alive init true");
	Check(enoki::hsum(ray.depth * ray.depth) == 0.f, "depth init zero");

	// HitP validity against maxt.
	HitP hit;
	hit.t = FloatP(1.5f);
	Check(enoki::all(hit.IsValid(FloatP(10.f))), "hit within maxt valid");
	Check(!enoki::any(hit.IsValid(FloatP(1.f))), "hit beyond maxt invalid");
}

// rng.h: PCG32 produces varied, in-range values; advance is stable.
void CheckRNG() {
	RNGP rng(0x12345678ULL);
	FloatP r = rng.NextFloat();
	Check(enoki::all(r >= FloatP(0.f)) && enoki::all(r < FloatP(1.f)),
		"rng float in [0,1)");

	// Two consecutive draws should differ in at least one lane.
	FloatP r2 = rng.NextFloat();
	Check(!enoki::all(r == r2), "rng varies between draws");

	// Different seeds -> different streams.
	RNGP rng2(0x9abcdef0ULL);
	FloatP r3 = rng2.NextFloat();
	Check(!enoki::all(r == r3), "rng seed changes stream");

	// A masked draw must still yield in-range values.
	RNGP rng3(0xdeadbeefULL);
	FloatP rm = rng3.NextFloat(MaskP(true));
	Check(enoki::all(rm >= FloatP(0.f)) && enoki::all(rm < FloatP(1.f)),
		"masked rng float in [0,1)");
}

} // anonymous namespace

int main() {
	std::cout << "lux2 Phase 1 SoA core check (lanes=" << PACKET_WIDTH << ")"
		<< std::endl;

	CheckVecP();
	CheckGeometry();
	CheckSpectrum();
	CheckRay();
	CheckRNG();

	if (g_failures == 0) {
		std::cout << "lux2corecheck: ALL CHECKS PASSED" << std::endl;
		return EXIT_SUCCESS;
	}
	std::cerr << "lux2corecheck: " << g_failures << " CHECK(S) FAILED"
		<< std::endl;
	return EXIT_FAILURE;
}
