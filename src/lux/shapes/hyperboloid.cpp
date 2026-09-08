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

// hyperboloid.cpp*
#include "hyperboloid.h"
#include "paramset.h"
#include "dynload.h"

using namespace luxrays;
using namespace lux;

// Hyperboloid Method Definitions
Hyperboloid::Hyperboloid(const Transform &o2w, bool ro, const string &name, 
		const Point &point1, const Point &point2, float tm)
	: Shape(o2w, ro, name) {
	p1 = point1;
	p2 = point2;
	phiMax = Radians(Clamp(tm, 0.0f, 360.0f));
	float rad_1 = sqrtf(p1.x*p1.x + p1.y*p1.y);
	float rad_2 = sqrtf(p2.x*p2.x + p2.y*p2.y);
	rmax = max(rad_1,rad_2);
	zmin = min(p1.z,p2.z);
	zmax = max(p1.z,p2.z);
	// Compute implicit function coefficients for hyperboloid
	if (p2.z == 0.) swap(p1, p2);
	Point pp = p1;
	float xy1, xy2;
	do {
		pp += 2.f * (p2-p1);
		xy1 = pp.x*pp.x + pp.y*pp.y;
		xy2 = p2.x*p2.x + p2.y*p2.y;
		a = (1.f/xy1 - (pp.z*pp.z)/(xy1*p2.z*p2.z)) /
		    (1 - (xy2*pp.z*pp.z)/(xy1*p2.z*p2.z));
		c = (a * xy2 - 1) / (p2.z*p2.z);
	} while (isinf(a) || isnan(a));
}
BBox Hyperboloid::ObjectBound() const {
	Point p1 = Point( -rmax, -rmax, zmin );
	Point p2 = Point(  rmax,  rmax, zmax );
	return BBox( p1, p2 );
}
bool Hyperboloid::Intersect(const Ray &r, float *tHit,
		DifferentialGeometry *dg) const {
	float phi, v;
	Point phit;
	// Transform _Ray_ to object space
	Ray ray(Inverse(ObjectToWorld) * r);
	// Compute quadratic hyperboloid coefficients
	float A = a*ray.d.x*ray.d.x +
	          a*ray.d.y*ray.d.y -
	          c*ray.d.z*ray.d.z;
	float B = 2.f * (a*ray.d.x*ray.o.x +
	                 a*ray.d.y*ray.o.y -
	                 c*ray.d.z*ray.o.z);
	float C = a*ray.o.x*ray.o.x +
	          a*ray.o.y*ray.o.y -
	          c*ray.o.z*ray.o.z - 1;
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
	// Compute hyperboloid inverse mapping
	phit = ray(thit);
	v = (phit.z - p1.z)/(p2.z - p1.z);
	Point pr = (1.f-v) * p1 + v * p2;
	phi = atan2f(pr.x*phit.y - phit.x*pr.y,
		phit.x*pr.x + phit.y*pr.y);
	if (phi < 0)
		phi += 2*M_PI;
	// Test hyperboloid intersection against clipping parameters
	if (phit.z < zmin || phit.z > zmax || phi > phiMax) {
		if (thit == t1) return false;
		thit = t1;
		if (t1 > ray.maxt) return false;
		// Compute hyperboloid inverse mapping
		phit = ray(thit);
		v = (phit.z - p1.z)/(p2.z - p1.z);
		Point pr = (1.f-v) * p1 + v * p2;
		phi = atan2f(pr.x*phit.y - phit.x*pr.y,
			phit.x*pr.x + phit.y*pr.y);
		if (phi < 0)
			phi += 2*M_PI;
		if (phit.z < zmin || phit.z > zmax || phi > phiMax)
			return false;
	}
	// Compute parametric representation of hyperboloid hit
	float u = phi / phiMax;
	// Compute hyperboloid \dpdu and \dpdv
	float cosphi = cosf(phi), sinphi = sinf(phi);
	Vector dpdu(-phiMax * phit.y, phiMax * phit.x, 0.);
	Vector dpdv((p2.x-p1.x) * cosphi - (p2.y-p1.y) * sinphi,
		(p2.x-p1.x) * sinphi + (p2.y-p1.y) * cosphi,
		p2.z-p1.z);
	// Compute hyperboloid \dndu and \dndv
	Vector d2Pduu = -phiMax * phiMax *
	                Vector(phit.x, phit.y, 0);
	Vector d2Pduv = phiMax *
	                Vector(-dpdv.y, dpdv.x, 0.);
	Vector d2Pdvv(0, 0, 0);
	// Compute coefficients for fundamental forms
	float E = Dot(dpdu, dpdu);
	float F = Dot(dpdu, dpdv);
	float G = Dot(dpdv, dpdv);
	Vector N = Normalize(Cross(dpdu, dpdv));
	float e = Dot(N, d2Pduu);
	float f = Dot(N, d2Pduv);
	float g = Dot(N, d2Pdvv);
	// Compute \dndu and \dndv from fundamental form coefficients
	float invEGF2 = 1.f / (E*G - F*F);
	Normal dndu((f*F - e*G) * invEGF2 * dpdu +
		(e*F - f*E) * invEGF2 * dpdv);
	Normal dndv((g*F - f*G) * invEGF2 * dpdu +
		(f*F - g*E) * invEGF2 * dpdv);
	// Initialize _DifferentialGeometry_ from parametric information
	*dg = DifferentialGeometry(ObjectToWorld * phit,
		ObjectToWorld * dpdu, ObjectToWorld * dpdv,
		ObjectToWorld * dndu, ObjectToWorld * dndv,
		u, v, this);
	// Update _tHit_ for quadric intersection
	*tHit = thit;
	return true;
}
bool Hyperboloid::IntersectP(const Ray &r) const {
	float phi, v;
	Point phit;
	// Transform _Ray_ to object space
	Ray ray(Inverse(ObjectToWorld) * r);
	// Compute quadratic hyperboloid coefficients
	float A = a*ray.d.x*ray.d.x +
	          a*ray.d.y*ray.d.y -
	          c*ray.d.z*ray.d.z;
	float B = 2.f * (a*ray.d.x*ray.o.x +
	                 a*ray.d.y*ray.o.y -
	                 c*ray.d.z*ray.o.z);
	float C = a*ray.o.x*ray.o.x +
	          a*ray.o.y*ray.o.y -
	          c*ray.o.z*ray.o.z - 1;
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
	// Compute hyperboloid inverse mapping
	phit = ray(thit);
	v = (phit.z - p1.z)/(p2.z - p1.z);
	Point pr = (1.f-v) * p1 + v * p2;
	phi = atan2f(pr.x*phit.y - phit.x*pr.y,
		phit.x*pr.x + phit.y*pr.y);
	if (phi < 0)
		phi += 2*M_PI;
	// Test hyperboloid intersection against clipping parameters
	if (phit.z < zmin || phit.z > zmax || phi > phiMax) {
		if (thit == t1) return false;
		thit = t1;
		if (t1 > ray.maxt) return false;
		// Compute hyperboloid inverse mapping
		phit = ray(thit);
		v = (phit.z - p1.z)/(p2.z - p1.z);
		Point pr = (1.f-v) * p1 + v * p2;
		phi = atan2f(pr.x*phit.y - phit.x*pr.y,
			phit.x*pr.x + phit.y*pr.y);
		if (phi < 0)
			phi += 2*M_PI;
		if (phit.z < zmin || phit.z > zmax || phi > phiMax)
			return false;
	}
	return true;
}
#define SQR(a) ((a)*(a))
#define QUAD(a) ((SQR(a))*(SQR(a)))
float Hyperboloid::Area() const {
	return phiMax/6.f *
	   (2.f*QUAD(p1.x) - 2.f*p1.x*p1.x*p1.x*p2.x +
		 2.f*QUAD(p2.x) +
		    2.f*(p1.y*p1.y + p1.y*p2.y + p2.y*p2.y)*
			(SQR(p1.y - p2.y) + SQR(p1.z - p2.z)) +
		   p2.x*p2.x*(5.f*p1.y*p1.y + 2.f*p1.y*p2.y -
		      4.f*p2.y*p2.y + 2.f*SQR(p1.z - p2.z)) +
		   p1.x*p1.x*(-4.f*p1.y*p1.y + 2.f*p1.y*p2.y +
		      5.f*p2.y*p2.y + 2.f*SQR(p1.z - p2.z)) -
		   2.f*p1.x*p2.x*(p2.x*p2.x - p1.y*p1.y +
		      5.f*p1.y*p2.y - p2.y*p2.y - p1.z*p1.z +
			  2.f*p1.z*p2.z - p2.z*p2.z));
}
#undef SQR
#undef QUAD
void Hyperboloid::Refine(vector<boost::shared_ptr<Shape> > &refined) const {
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
			const Point pr = (1.f - vv) * p1 + vv * p2;
			const float cosphi = cosf(phi), sinphi = sinf(phi);
			const int idx = v * NU + u;
			P[idx] = Point(pr.x * cosphi - pr.y * sinphi,
			               pr.x * sinphi + pr.y * cosphi,
			               pr.z);
			// Compute normal from dpdu x dpdv (object space)
			Vector dpdu(-phiMax * P[idx].y, phiMax * P[idx].x, 0.f);
			Vector dpdv((p2.x - p1.x) * cosphi - (p2.y - p1.y) * sinphi,
			            (p2.x - p1.x) * sinphi + (p2.y - p1.y) * cosphi,
			            p2.z - p1.z);
			N[idx] = Normalize(Normal(Cross(dpdu, dpdv)));
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
Shape* Hyperboloid::CreateShape(const Transform &o2w,
		bool reverseOrientation, const ParamSet &params) {
	string name = params.FindOneString("name", "'hyperboloid'");
	Point p1 = params.FindOnePoint( "p1", Point(0,0,0) );
	Point p2 = params.FindOnePoint( "p2", Point(1,1,1) );
	float phimax = params.FindOneFloat( "phimax", 360 );
	return new Hyperboloid(o2w, reverseOrientation, name, p1, p2, phimax);
}

static DynamicLoader::RegisterShape<Hyperboloid> r("hyperboloid");
