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

// True if scalars 'a' and 'b' are within 'eps'.
bool Close(float a, float b, float eps = 1e-4f) {
	return std::fabs(a - b) < eps;
}

// True if two 3D points agree component-wise within 'eps'.
bool Close(const Point3f &a, const Point3f &b, float eps = 1e-4f) {
	return Close(a.x(), b.x(), eps) && Close(a.y(), b.y(), eps) &&
	       Close(a.z(), b.z(), eps);
}

// True if two 3D vectors agree component-wise within 'eps'.
bool Close(const Vector3f &a, const Vector3f &b, float eps = 1e-4f) {
	return Close(a.x(), b.x(), eps) && Close(a.y(), b.y(), eps) &&
	       Close(a.z(), b.z(), eps);
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
	SWCSpectrumP s{FloatP(a)};              // Brace-init avoids the vexing parse.
	SWCSpectrumP t = s * s;                 // a^2 per sample.
	SWCSpectrumP u = s + t;                 // a + a^2.
	Check(AllClose(u[0], a + a * a), "spectrum mul/add");

	Check(AllClose(s.MaxComponent(), a), "spectrum MaxComponent");
	Check(AllClose(s.Average(), a), "spectrum Average");

	SWCSpectrumP black{0.f};
	Check(enoki::all(black.IsBlack()), "black spectrum IsBlack");
	Check(!enoki::all(s.IsBlack()), "non-black spectrum !IsBlack");

	SWCSpectrumP clamped = (s * -1.f).Clamped();
	Check(enoki::all(clamped[0] == FloatP(0.f)), "Clamped to zero");

	// Enoki select + fmadd (broadcast over the wavelength axis).
	const MaskP m = s[0] > FloatP(a * 0.5f);
	SWCSpectrumP sel = enoki::select(m, s, black);
	Check(AllClose(sel[0], a), "spectrum select");
	SWCSpectrumP f = enoki::fmadd(s, s, s);  // a^2 + a
	Check(AllClose(f[0], a * a + a), "spectrum fmadd");
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

	// ray(2) == o + d*2 -> (a, 2, 0).
	Point3fP pos = ray(FloatP(2.f));
	Check(AllClose(pos.x(), a), "ray(t).x");
	Check(AllClose(pos.y(), 2.f), "ray(t).y");

	// Payload initialized.
	Check(enoki::all(ray.throughput[0] == FloatP(1.f)), "throughput init");
	Check(enoki::all(ray.alive), "alive init true");
	Check(enoki::hsum(ray.depth * ray.depth) == 0.f, "depth init zero");

	// HitP hit-mask field.
	HitP hit;
	hit.t = FloatP(1.5f);
	hit.hit = MaskP(true);
	Check(enoki::all(hit.hit), "hit mask set");
	Check(AllClose(hit.t, 1.5f), "hit t stored");
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

// geometry.h: scalar Point/Vector/Normal and UV.
void CheckGeometryScalar() {
	const Point3f p(1.f, 2.f, 3.f);
	const Point3f q(0.f, 0.f, 0.f);
	const Vector3f d = p - q;
	Check(Close(d, Vector3f(1.f, 2.f, 3.f)), "scalar point-point == vector");

	const Point3f r = q + Vector3f(1.f, 1.f, 1.f);
	Check(Close(r, Point3f(1.f, 1.f, 1.f)), "scalar point+vector");

	const UV uv(0.25f, 0.75f);
	Check(Close(uv.u, 0.25f) && Close(uv.v, 0.75f), "scalar uv fields");
}

// spectrum.h: scalar SWCSpectrum arithmetic and reductions.
void CheckSpectrumScalar() {
	const SWCSpectrum s(2.f);
	const SWCSpectrum t = s * s;            // 4 per sample.
	const SWCSpectrum u = s + t;            // 6 per sample.
	Check(Close(u[0], 6.f), "scalar spectrum mul/add");

	Check(Close(s.MaxComponent(), 2.f), "scalar spectrum MaxComponent");
	Check(Close(s.Average(), 2.f), "scalar spectrum Average");

	const SWCSpectrum black(0.f);
	Check(black.IsBlack(), "scalar black IsBlack");
	Check(!s.IsBlack(), "scalar non-black !IsBlack");

	const SWCSpectrum neg = (s * -1.f).Clamped();
	Check(Close(neg[0], 0.f), "scalar Clamped to zero");

	const RGBColor rgb(1.f, 0.5f, 0.f);
	Check(Close(rgb.r(), 1.f) && Close(rgb.g(), 0.5f) && Close(rgb.b(), 0.f),
		"scalar RGBColor fields");
	Check(Close(rgb.Y(), 0.212671f * 1.f + 0.715160f * 0.5f + 0.072169f * 0.f),
		"scalar RGBColor Y");
}

// bbox.h: BBox construction, queries, and unions.
void CheckBBox() {
	const BBox unit(Point3f(0.f, 0.f, 0.f), Point3f(1.f, 1.f, 1.f));
	Check(unit.IsValid(), "bbox valid");
	Check(Close(unit.Volume(), 1.f), "bbox volume");
	Check(Close(unit.SurfaceArea(), 6.f), "bbox surface area");
	Check(Close(unit.Center(), Point3f(.5f, .5f, .5f)), "bbox center");

	// A box elongated along X reports axis 0 as the largest extent.
	const BBox wide(Point3f(0.f, 0.f, 0.f), Point3f(2.f, 1.f, 1.f));
	Check(wide.MaximumExtent() == 0, "bbox maximum extent == X");

	Check(unit.Inside(Point3f(.5f, .5f, .5f)), "bbox inside point");
	Check(!unit.Inside(Point3f(2.f, .5f, .5f)), "bbox outside point");

	const BBox a(Point3f(0.f, 0.f, 0.f), Point3f(2.f, 2.f, 2.f));
	const BBox b(Point3f(1.f, 1.f, 1.f), Point3f(3.f, 3.f, 3.f));
	const BBox c(Point3f(5.f, 5.f, 5.f), Point3f(6.f, 6.f, 6.f));
	Check(a.Overlaps(b), "bbox overlaps");
	Check(!a.Overlaps(c), "bbox disjoint");

	const BBox u = Union(a, b);
	Check(Close(u.pMin, Point3f(0.f, 0.f, 0.f)) &&
	      Close(u.pMax, Point3f(3.f, 3.f, 3.f)), "bbox union");

	BBox grown(Point3f(0.f, 0.f, 0.f), Point3f(1.f, 1.f, 1.f));
	grown.Expand(1.f);
	Check(Close(grown.pMin, Point3f(-1.f, -1.f, -1.f)) &&
	      Close(grown.pMax, Point3f(2.f, 2.f, 2.f)), "bbox expand");
}

// transform.h: apply, concat, inverse, look_at, and the normal rule.
void CheckTransform() {
	// Translate moves points but not vectors.
	const Transform t = Transform::translate(Vector3f(1.f, 2.f, 3.f));
	Check(Close(t * Point3f(0.f, 0.f, 0.f), Point3f(1.f, 2.f, 3.f)),
		"translate point");
	Check(Close(t * Vector3f(0.f, 0.f, 0.f), Vector3f(0.f, 0.f, 0.f)),
		"translate ignores vector");

	// Scale stretches vectors.
	const Transform s = Transform::scale(Vector3f(2.f, 3.f, 4.f));
	Check(Close(s * Vector3f(1.f, 1.f, 1.f), Vector3f(2.f, 3.f, 4.f)),
		"scale vector");

	// Rotate 90 deg about +Z maps +X to +Y.
	const Transform rz = Transform::rotate(Vector3f(0.f, 0.f, 1.f), 90.f);
	Check(Close(rz * Vector3f(1.f, 0.f, 0.f), Vector3f(0.f, 1.f, 0.f)),
		"rotate Z 90: +X -> +Y");

	// Normals transform by the inverse transpose: the scale (2,3,4) maps the
	// normal (1,1,0) to (1/2, 1/3, 0). Getting this wrong silently corrupts
	// every shading normal.
	const Normal3f n = s * Normal3f(1.f, 1.f, 0.f);
	Check(Close(n.x(), 0.5f) && Close(n.y(), 1.f / 3.f) && Close(n.z(), 0.f),
		"normal uses inverse transpose under non-uniform scale");

	// Concatenation: translate then scale, applied to a point.
	const Transform ts = s * t;
	Check(Close(ts * Point3f(0.f, 0.f, 0.f), Point3f(2.f, 6.f, 12.f)),
		"transform concat (scale*translate)");

	// Inverse round-trips a point.
	const Transform m = ts.inverse();
	Check(Close(m * (ts * Point3f(1.5f, -2.f, 0.25f)),
	            Point3f(1.5f, -2.f, 0.25f)),
		"transform inverse round-trip");

	// Hand-computed look_at: camera at origin looking down -Z with +Y up.
	// The camera-to-world basis is left=(-1,0,0), up=(0,1,0), dir=(0,0,-1),
	// so a camera-space point (1,0,0) maps to world (-1,0,0), and the inverse
	// (world-to-camera) maps it back.
	const Transform c2w = Transform::look_at(Point3f(0.f, 0.f, 0.f),
		Point3f(0.f, 0.f, -1.f), Vector3f(0.f, 1.f, 0.f));
	Check(Close(c2w * Point3f(1.f, 0.f, 0.f), Point3f(-1.f, 0.f, 0.f)),
		"look_at camera->world");
	Check(Close(c2w.inverse() * Point3f(-1.f, 0.f, 0.f), Point3f(1.f, 0.f, 0.f)),
		"look_at world->camera");

	// LookAt composed with a translation: moving the camera along its own +X
	// (which is world -X) shifts the world image accordingly.
	const Transform moved = c2w * Transform::translate(Vector3f(1.f, 0.f, 0.f));
	Check(Close(moved * Point3f(0.f, 0.f, 0.f), Point3f(-1.f, 0.f, 0.f)),
		"look_at * translate");
}

} // anonymous namespace

int main() {
	std::cout << "lux2 Phase 1 SoA core check (lanes=" << PACKET_WIDTH << ")"
		<< std::endl;

	CheckVecP();
	CheckGeometry();
	CheckGeometryScalar();
	CheckSpectrum();
	CheckSpectrumScalar();
	CheckBBox();
	CheckTransform();
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
