/***************************************************************************
 *   Copyright (C) 1998-2013 by authors (see AUTHORS.txt)                  *
 *                                                                         *
 *   This file is part of LuxRender.                                       *
 *                                                                         *
 *   Lux Renderer is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   Lux Renderer is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. *
 *                                                                         *
 *   This project is based on PBRT ; see http://www.pbrt.org               *
 *   Lux Renderer website : http://www.luxrender.net                       *
 ***************************************************************************/

// disk.cpp*
#include "disk.h"
#include "paramset.h"
#include "dynload.h"

using namespace luxrays;
using namespace lux;

// Disk Method Definitions
Disk::Disk(const Transform &o2w, bool ro, const string &name, 
           float ht, float r, float ri, float tmax)
	: Shape(o2w, ro, name) {
	height = ht;
	radius = r;
	innerRadius = ri;
	phiMax = Radians(Clamp(tmax, 0.0f, 360.0f));
}
BBox Disk::ObjectBound() const {
	return BBox(Point(-radius, -radius, height),
				Point(radius, radius, height));
}
bool Disk::Intersect(const Ray &r, float *tHit,
		DifferentialGeometry *dg) const {
	// Transform _Ray_ to object space
	Ray ray(Inverse(ObjectToWorld) * r);
	// Compute plane intersection for disk
	if (fabsf(ray.d.z) < 1e-7) return false;
	float thit = (height - ray.o.z) / ray.d.z;
	if (thit < ray.mint || thit > ray.maxt)
		return false;
	// See if hit point is inside disk radii and \phimax
	Point phit = ray(thit);
	float dist2 = phit.x * phit.x + phit.y * phit.y;
	if (dist2 > radius * radius ||
	    dist2 < innerRadius*innerRadius)
		return false;
	// Test disk $\phi$ value against \phimax
	float phi = atan2f(phit.y, phit.x);
	if (phi < 0.f)
		phi += 2.f * M_PI;
	if (phi > phiMax)
		return false;
	// Find parametric representation of disk hit
	float u = phi / phiMax;
	float v = 1.f - ((sqrtf(dist2)-innerRadius) /
	                 (radius-innerRadius));
	Vector dpdu(-phiMax * phit.y, phiMax * phit.x, 0.);
	dpdu *= phiMax * INV_TWOPI;
	Vector dpdv(-phit.x / (1-v), -phit.y / (1-v), 0.);
	dpdv *= (radius - innerRadius) / radius;
	Normal dndu(0,0,0), dndv(0,0,0);
	// Initialize _DifferentialGeometry_ from parametric information
	*dg = DifferentialGeometry(ObjectToWorld * phit,
		ObjectToWorld * dpdu, ObjectToWorld * dpdv,
		ObjectToWorld * dndu, ObjectToWorld * dndv,
		u, v, this);
	// Update _tHit_ for quadric intersection
	*tHit = thit;
	return true;
}
bool Disk::IntersectP(const Ray &r) const {
	// Transform _Ray_ to object space
	Ray ray(Inverse(ObjectToWorld) * r);
	// Compute plane intersection for disk
	if (fabsf(ray.d.z) < 1e-7) return false;
	float thit = (height - ray.o.z) / ray.d.z;
	if (thit < ray.mint || thit > ray.maxt)
		return false;
	// See if hit point is inside disk radii and \phimax
	Point phit = ray(thit);
	float dist2 = phit.x * phit.x + phit.y * phit.y;
	if (dist2 > radius * radius ||
	    dist2 < innerRadius*innerRadius)
		return false;
	// Test disk $\phi$ value against \phimax
	float phi = atan2f(phit.y, phit.x);
	if (phi < 0.f)
		phi += 2.f * M_PI;
	if (phi > phiMax)
		return false;
	return true;
}
float Disk::Area() const {
	return phiMax * 0.5f *
            (radius * radius -innerRadius * innerRadius );
}
void Disk::Refine(vector<boost::shared_ptr<Shape> > &refined) const {
	const u_int NU = 64;
	const u_int NV = 32;
	const u_int nVerts = NU * NV;

	Point *P = new Point[nVerts];
	float *uvs = new float[2 * nVerts];
	Normal *N = new Normal[nVerts];

	for (u_int v = 0; v < NV; ++v) {
		for (u_int u = 0; u < NU; ++u) {
			const float uu = static_cast<float>(u) / static_cast<float>(NU - 1);
			const float vv = static_cast<float>(v) / static_cast<float>(NV - 1);
			const float phi = uu * phiMax;
			const float r = innerRadius + vv * (radius - innerRadius);
			const int idx = v * NU + u;
			P[idx] = Point(r * cosf(phi), r * sinf(phi), height);
			N[idx] = Normal(0.f, 0.f, 1.f);
			uvs[2 * idx] = uu;
			uvs[2 * idx + 1] = 1.f - vv;
		}
	}

	const u_int nTris = 2 * (NU - 1) * (NV - 1);
	int *verts = new int[3 * nTris];
	int *vp = verts;
	for (u_int v = 0; v < NV - 1; ++v) {
		for (u_int u = 0; u < NU - 1; ++u) {
#define VERT(u,v) static_cast<int>((v)*NU+(u))
			*vp++ = VERT(u, v);
			*vp++ = VERT(u+1, v);
			*vp++ = VERT(u+1, v+1);

			*vp++ = VERT(u, v);
			*vp++ = VERT(u+1, v+1);
			*vp++ = VERT(u, v+1);
		}
#undef VERT
	}

	ParamSet paramSet;
	paramSet.AddInt("indices", verts, 3 * nTris);
	paramSet.AddFloat("uv", uvs, 2 * nVerts);
	paramSet.AddPoint("P", P, nVerts);
	paramSet.AddNormal("N", N, nVerts);
	refined.push_back(MakeShape("trianglemesh",
			ObjectToWorld, reverseOrientation, paramSet));

	delete[] P;
	delete[] uvs;
	delete[] N;
	delete[] verts;
}
Shape* Disk::CreateShape(const Transform &o2w,
		bool reverseOrientation, const ParamSet &params) {
	string name = params.FindOneString("name", "'disk'");
	float height = params.FindOneFloat( "height", 0. );
	float radius = params.FindOneFloat( "radius", 1 );
	float inner_radius = params.FindOneFloat( "innerradius", 0 );
	float phimax = params.FindOneFloat( "phimax", 360 );
	return new Disk(o2w, reverseOrientation, name, height, radius, inner_radius, phimax);
}

static DynamicLoader::RegisterShape<Disk> r("disk");
