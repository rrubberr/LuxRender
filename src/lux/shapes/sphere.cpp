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

// sphere.cpp*
#include "sphere.h"
#include "paramset.h"
#include "dynload.h"

using namespace luxrays;
using namespace lux;

// Sphere Method Definitions
Sphere::Sphere(const Transform &o2w, bool ro, const string &name, 
	           float rad, float z0, float z1, float pm)
	: Shape(o2w, ro, name)
{
	radius = rad;
	zMin = Clamp(min(z0, z1), -radius, radius);
	zMax = Clamp(max(z0, z1), -radius, radius);
	thetaMin = acosf(Clamp(zMin / radius, -1.f, 1.f));
	thetaMax = acosf(Clamp(zMax / radius, -1.f, 1.f));
	phiMax = Radians(Clamp(pm, 0.f, 360.f));
}
BBox Sphere::ObjectBound() const
{
	return BBox(Point(-radius, -radius, zMin),
		Point(radius,  radius, zMax));
}
bool Sphere::Intersect(const Ray &r, Intersection *isect) const
{
	// Transform _Ray_ to object space
	const Ray ray(Inverse(ObjectToWorld) * r);
	// Compute quadratic sphere coefficients
	const float radius2 = radius * radius;
	const float A = ray.d.LengthSquared();
	const float B = 2.f * Dot(ray.d, Vector(ray.o));
	const float C = Vector(ray.o).LengthSquared() - radius2;
	// Solve quadratic equation for _t_ values
	float t0, t1;
	if (!Quadratic(A, B, C, &t0, &t1))
		return false;
	// Compute intersection distance along ray
	if (t0 > ray.maxt || t1 < ray.mint)
		return false;
	float tHit = t0;
	if (t0 < ray.mint) {
		if (t1 > ray.maxt)
			return false;
		tHit = t1;
	}
	// Compute sphere hit position and $\phi$
	Point pHit(ray(tHit));
	float phi = atan2f(pHit.y, pHit.x);
	if (phi < 0.f)
		phi += 2.f * M_PI;
	// Test sphere intersection against clipping parameters
	if (pHit.z < zMin || pHit.z > zMax || phi > phiMax) {
		if (tHit == t1 || t1 > ray.maxt)
			return false;
		tHit = t1;
		// Compute sphere hit position and $\phi$
		pHit = ray(tHit);
		phi = atan2f(pHit.y, pHit.x);
		if (phi < 0.f)
			phi += 2.f * M_PI;
		if (pHit.z < zMin || pHit.z > zMax || phi > phiMax)
			return false;
	}
	// Update _r.maxt_ for quadric intersection
	r.maxt = tHit;
	// Find parametric representation of sphere hit
	const float u = phi / phiMax;
	const float theta = acosf(Clamp(pHit.z / radius, -1.f, 1.f));
	const float v = (theta - thetaMin) / (thetaMax - thetaMin);
	// Compute sphere \dpdu and \dpdv
	const float Z = zMax - zMin;
	const float zRadius2 = max(0.f, radius2 - pHit.z * pHit.z);
	const float factor = -Z * pHit.z / zRadius2;
	const Vector dpdu(factor * pHit.x, factor * pHit.y, Z);
	const Vector dpdv(-phiMax * pHit.y, phiMax * pHit.x, 0.f);
	// Initialize _DifferentialGeometry_ from parametric information
	isect->dg = DifferentialGeometry(ObjectToWorld * pHit,
		Normalize(ObjectToWorld * Normal(pHit.x, pHit.y, pHit.z)),
		ObjectToWorld * dpdu, ObjectToWorld * dpdv,
		ObjectToWorld * Normal(dpdu / radius),
		ObjectToWorld * Normal(dpdv / radius),
		u, v, this);
	isect->dg.AdjustNormal(reverseOrientation, transformSwapsHandedness);
	isect->Set(ObjectToWorld, this, GetMaterial(),
		GetExterior(), GetInterior());
	return true;
}
bool Sphere::IntersectP(const Ray &r) const
{
	float phi;
	Point phit;
	// Transform _Ray_ to object space
	Ray ray(Inverse(ObjectToWorld) * r);
	// Compute quadratic sphere coefficients
	float A = ray.d.x*ray.d.x + ray.d.y*ray.d.y +
	          ray.d.z*ray.d.z;
	float B = 2 * (ray.d.x*ray.o.x + ray.d.y*ray.o.y +
				   ray.d.z*ray.o.z);
	float C = ray.o.x*ray.o.x + ray.o.y*ray.o.y +
			  ray.o.z*ray.o.z - radius*radius;
	// Solve quadratic equation for _t_ values
	float t0, t1;
	if (!Quadratic(A, B, C, &t0, &t1))
		return false;
	// Compute intersection distance along ray
	if (t0 > ray.maxt || t1 < ray.mint)
		return false;
	float thit = t0;
	if (t0 < ray.mint) {
		thit = t1;
		if (thit > ray.maxt) return false;
	}
	// Compute sphere hit position and $\phi$
	phit = ray(thit);
	phi = atan2f(phit.y, phit.x);
	if (phi < 0.) phi += 2.f*M_PI;
	// Test sphere intersection against clipping parameters
	if (phit.z < zMin || phit.z > zMax || phi > phiMax) {
		if (thit == t1) return false;
		if (t1 > ray.maxt) return false;
		thit = t1;
		// Compute sphere hit position and $\phi$
		phit = ray(thit);
		phi = atan2f(phit.y, phit.x);
		if (phi < 0.) phi += 2.f*M_PI;
		if (phit.z < zMin || phit.z > zMax || phi > phiMax)
			return false;
	}
	return true;
}
float Sphere::Area() const
{
	return phiMax * radius * (zMax - zMin);
}
void Sphere::Refine(vector<boost::shared_ptr<Shape> > &refined) const {
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
			const float theta = thetaMin + vv * (thetaMax - thetaMin);
			const float sinTheta = sinf(theta);
			const float cosTheta = cosf(theta);
			const float cosPhi = cosf(phi);
			const float sinPhi = sinf(phi);
			const int idx = v * NU + u;
			P[idx] = Point(radius * sinTheta * cosPhi,
			               radius * sinTheta * sinPhi,
			               radius * cosTheta);
			N[idx] = Normalize(Normal(P[idx].x, P[idx].y, P[idx].z));
			uvs[2 * idx] = uu;
			uvs[2 * idx + 1] = vv;
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

Shape* Sphere::CreateShape(const Transform &o2w, bool reverseOrientation,
	const ParamSet &params)
{
	string name = params.FindOneString("name", "'sphere'");
	float radius = params.FindOneFloat("radius", 1.f);
	float zmin = params.FindOneFloat("zmin", -radius);
	float zmax = params.FindOneFloat("zmax", radius);
	float phimax = params.FindOneFloat("phimax", 360.f);
	return new Sphere(o2w, reverseOrientation, name, radius, zmin, zmax, phimax);
}

static DynamicLoader::RegisterShape<Sphere> r("sphere");
