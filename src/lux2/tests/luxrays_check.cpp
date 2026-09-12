/***************************************************************************
 *   Copyright (C) 1998-2026 by authors (see AUTHORS.txt)                  #
 *                                                                         #
 *   This file is part of LuxRender.                                       #
 *                                                                         #
 *   LuxRender is free software; you can redistribute it and/or modify     #
 *   it under the terms of the GNU General Public License as published by  #
 *   the Free Software Foundation; either version 3 of the License, or     #
 *   any later version.                                                    #
 *                                                                         #
 *   LuxRender is distributed in the hope that it will be useful,          #
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        #
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the          #
 *   GNU General Public License for more details.                          #
 *                                                                         #
 *   You should have received a copy of the GNU General Public License     #
 *   along with this program. If not, see <http://www.gnu.org/licenses/>   #
 *                                                                         #
 *   This project is based on PBRT; see <http://www.pbrt.org>              #
 ***************************************************************************/

// Tests calar luxrays types (Point/Vector/Normal/Transform),
// SWCSpectrum + SpectrumWavelengths, TriangleMesh, and rply read.

#include "luxrays/cfg.h"
#include "luxrays/core/geometry/point.h"
#include "luxrays/core/geometry/vector.h"
#include "luxrays/core/geometry/normal.h"
#include "luxrays/core/geometry/transform.h"
#include "luxrays/core/color/swcspectrum.h"
#include "luxrays/core/color/spectrumwavelengths.h"
#include "luxrays/core/trianglemesh.h"
#include "luxrays/utils/ply/rply.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

using namespace luxrays;

// C callback for rply vertex counting.
static int PlyVertexCountCB(p_ply_argument argument) {
	void *pdata = nullptr;
	ply_get_argument_user_data(argument, &pdata, nullptr);
	long *count = (long *)pdata;
	*count = *count + 1;
	return 1;
}

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

// luxrays/cfg.h
void CheckVersion() {
	std::cout << "[cfg] LUXRAYS_VERSION="
		<< LUXRAYS_VERSION_MAJOR << "." << LUXRAYS_VERSION_MINOR << std::endl;
	Check(std::strcmp(LUXRAYS_VERSION_MAJOR, "2") == 0, "version major == 2");
	Check(std::strcmp(LUXRAYS_VERSION_MINOR, "6") == 0, "version minor == 6");
}

// Point / Vector / Normal / Transform
void CheckGeometry() {
	// Point construction and arithmetic.
	Point p(1.f, 2.f, 3.f);
	Check(std::abs(p.x - 1.f) < 1e-6f, "Point.x");
	Check(std::abs(p.y - 2.f) < 1e-6f, "Point.y");
	Check(std::abs(p.z - 3.f) < 1e-6f, "Point.z");

	// Point + Vector == Point.
	Vector v(0.5f, -1.f, 2.f);
	Point q = p + v;
	Check(std::abs(q.x - 1.5f) < 1e-6f, "Point + Vector");

	// Point - Point == Vector.
	Vector d = q - p;
	Check(std::abs(d.x - 0.5f) < 1e-6f, "Point - Point == Vector");

	// Transform: Translate a point.
	float mat[4][4] = {
		{1, 0, 0, 10.f},
		{0, 1, 0, 20.f},
		{0, 0, 1, 30.f},
		{0, 0, 0, 1.f}
	};
	Transform t(mat);
	Point tp = t * p;
	Check(std::abs(tp.x - 11.f) < 1e-6f, "Transform translate x");
	Check(std::abs(tp.y - 22.f) < 1e-6f, "Transform translate y");
	Check(std::abs(tp.z - 33.f) < 1e-6f, "Transform translate z");

	// Transform * Vector.
	Vector tv = t * v;
	Check(std::abs(tv.x - 0.5f) < 1e-6f, "Transform * Vector x");
	Check(std::abs(tv.y + 1.f) < 1e-6f, "Transform * Vector y");
	Check(std::abs(tv.z - 2.f) < 1e-6f, "Transform * Vector z");

	// Normal construction and dot product.
	Normal n1(1.f, 0.f, 0.f);
	Normal n2(0.f, 1.f, 0.f);
	Check(std::abs(Dot(n1, n2)) < 1e-6f, "Normal dot orthogonal");

	Normal n3(1.f, 2.f, 3.f);
	Vector n3n = Normalize(Vector(n3.x, n3.y, n3.z));
	float len = std::sqrt(n3n.x * n3n.x + n3n.y * n3n.y + n3n.z * n3n.z);
	Check(std::abs(len - 1.f) < 1e-6f, "Normal normalize length == 1");
}

// SWCSpectrum / SpectrumWavelengths
void CheckSpectrum() {
	// Construct uniform SWCSpectrum.
	SWCSpectrum s(0.5f);
	Check(s.Black() == false, "SWCSpectrum(0.5) not black");
	Check(std::abs(s.Max() - 0.5f) < 1e-6f, "SWCSpectrum Max == 0.5");

	// Arithmetic.
	SWCSpectrum s2(0.3f);
	SWCSpectrum s3 = s + s2;
	Check(std::abs(s3.c[0] - 0.8f) < 1e-6f, "SWCSpectrum add");

	SWCSpectrum s4 = s * s2;
	Check(std::abs(s4.c[0] - 0.15f) < 1e-6f, "SWCSpectrum mul");

	// SpectrumWavelengths.
	SpectrumWavelengths sw;
	sw.Sample(0.5f);
	Check(sw.single == false, "SpectrumWavelengths Sample sets single=false");
	Check(sw.w[0] >= 380.f && sw.w[0] <= 720.f, "SpectrumWavelengths w in range");

	float single_w = sw.SampleSingle();
	Check(sw.single == true, "SpectrumWavelengths SampleSingle sets single=true");
	Check(single_w >= 380.f && single_w <= 720.f, "SpectrumWavelengths single_w in range");
}

// TriangleMesh
void CheckTriangleMesh() {
	// Build a single triangle mesh: a right triangle in the XY plane.
	const u_int vertCount = 3;
	const u_int triCount = 1;

	Point *verts = TriangleMesh::AllocVerticesBuffer(vertCount);
	verts[0] = Point(0.f, 0.f, 0.f);
	verts[1] = Point(1.f, 0.f, 0.f);
	verts[2] = Point(0.f, 1.f, 0.f);

	Triangle *tris = TriangleMesh::AllocTrianglesBuffer(triCount);
	tris[0] = Triangle(0, 1, 2);

	TriangleMesh mesh(vertCount, triCount, verts, tris);

	Check(mesh.GetTotalVertexCount() == 3u, "TriangleMesh vertex count");
	Check(mesh.GetTotalTriangleCount() == 1u, "TriangleMesh triangle count");

	// Verify vertex data.
	Point v0 = mesh.GetVertex(0.f, 0);
	Check(std::abs(v0.x) < 1e-6f && std::abs(v0.y) < 1e-6f && std::abs(v0.z) < 1e-6f,
		"TriangleMesh vertex 0");

	// Verify triangle indices.
	const Triangle *tris_arr = mesh.GetTriangles();
	Check(tris_arr[0].v[0] == 0 && tris_arr[0].v[1] == 1 && tris_arr[0].v[2] == 2,
		"TriangleMesh triangle indices");

	// BBox should be non-empty.
	BBox bbox = mesh.GetBBox();
	Check(bbox.pMin.x <= 1.f && bbox.pMax.x >= 1.f, "TriangleMesh BBox x");
	Check(bbox.pMin.y <= 1.f && bbox.pMax.y >= 1.f, "TriangleMesh BBox y");

	mesh.Delete();
}

// rply read
void CheckPLY(const char *plyPath) {
	if (!plyPath) {
		std::cerr << "  FAIL: no PLY path provided" << std::endl;
		++g_failures;
		return;
	}

	p_ply ply = ply_open(plyPath, [](const char *msg) {
		std::cerr << "  PLY error: " << msg << std::endl;
	});
	if (!ply) {
		std::cerr << "  FAIL: could not open PLY file: " << plyPath << std::endl;
		++g_failures;
		return;
	}

	if (!ply_read_header(ply)) {
		std::cerr << "  FAIL: could not read PLY header: " << plyPath << std::endl;
		ply_close(ply);
		++g_failures;
		return;
	}

	// Count vertices.
	long vertexCount = 0;
	ply_set_read_cb(ply, "vertex", "x", PlyVertexCountCB, &vertexCount, 0);

	if (!ply_read(ply)) {
		std::cerr << "  FAIL: could not read PLY data: " << plyPath << std::endl;
		ply_close(ply);
		++g_failures;
		return;
	}

	ply_close(ply);
	Check(vertexCount > 0, "PLY vertex count > 0");
	std::cout << "  ok:   PLY vertex count = " << vertexCount << std::endl;
}

} // anonymous namespace

int main(int argc, char *argv[]) {
	std::cout << "luxrays Phase 2a snapshot check" << std::endl;

	CheckVersion();
	CheckGeometry();
	CheckSpectrum();
	CheckTriangleMesh();

	// PLY path: argv[1] > hardcoded fallback.
	const char *plyPath = nullptr;
	if (argc > 1) {
		plyPath = argv[1];
	} else {
		plyPath = "test/cornell_box/Scene/00001/floor_0000_m000.ply";
	}
	std::cout << "[ply] path: " << plyPath << std::endl;
	CheckPLY(plyPath);

	if (g_failures == 0) {
		std::cout << "luxrays2check: ALL CHECKS PASSED" << std::endl;
		return EXIT_SUCCESS;
	}
	std::cerr << "luxrays2check: " << g_failures << " CHECK(S) FAILED" << std::endl;
	return EXIT_FAILURE;
}
