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

#include "shapes/sphere.h"

#include "core/dynload.h"
#include "core/paramset.h"
#include "core/register.h"

#include <cmath>

namespace lux2 {

namespace {
constexpr float kPi = 3.14159265358979323846f;
float Radians(float d) { return d * (kPi / 180.f); }
float Clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
} // namespace

SphereShape::SphereShape(const Transform &toWorld, float radius, float zMin,
                         float zMax, float phiMaxDeg, int phiSegments,
                         int thetaSegments)
    : m_toWorld(toWorld), m_radius(radius) {
    m_zMin = Clampf(zMin < zMax ? zMin : zMax, -radius, radius);
    m_zMax = Clampf(zMin < zMax ? zMax : zMin, -radius, radius);
    m_thetaMin = std::acos(Clampf(m_zMin / radius, -1.f, 1.f));
    m_thetaMax = std::acos(Clampf(m_zMax / radius, -1.f, 1.f));
    m_phiMax = Radians(Clampf(phiMaxDeg, 0.f, 360.f));
    m_nu = phiSegments > 1 ? phiSegments : 2;
    m_nv = thetaSegments > 1 ? thetaSegments : 2;
}

BBox SphereShape::WorldBound() const {
    // Object space bound of the sphere.
    const BBox ob(Point3f(-m_radius, -m_radius, m_zMin),
                  Point3f( m_radius,  m_radius, m_zMax));
    // Transform the 8 corners and union.
    BBox wb;
    for (int i = 0; i < 8; ++i) {
        const Point3f c((i & 1) ? ob.pMax.x() : ob.pMin.x(),
                        (i & 2) ? ob.pMax.y() : ob.pMin.y(),
                        (i & 4) ? ob.pMax.z() : ob.pMin.z());
        wb = Union(wb, m_toWorld * c);
    }
    return wb;
}

void SphereShape::Tessellate(const Transform & /*worldToCamera*/,
                             std::vector<TriangleDesc> &out) const {
    const int NU = m_nu, NV = m_nv;
    // Build the vertex/normal/uv grid (object space), then emit world-space
    // triangles.
    std::vector<Point3f> P(static_cast<size_t>(NU) * NV);
    std::vector<Normal3f> N(static_cast<size_t>(NU) * NV);
    std::vector<UV> uv(static_cast<size_t>(NU) * NV);

    for (int v = 0; v < NV; ++v) {
        for (int u = 0; u < NU; ++u) {
            const float uu = static_cast<float>(u) / static_cast<float>(NU - 1);
            const float vv = static_cast<float>(v) / static_cast<float>(NV - 1);
            const float phi = uu * m_phiMax;
            const float theta = m_thetaMin + vv * (m_thetaMax - m_thetaMin);
            const float sinT = std::sin(theta), cosT = std::cos(theta);
            const float cosP = std::cos(phi), sinP = std::sin(phi);
            const size_t idx = static_cast<size_t>(v) * NU + u;
            P[idx] = Point3f(m_radius * sinT * cosP,
                             m_radius * sinT * sinP,
                             m_radius * cosT);
            N[idx] = enoki::normalize(Normal3f(P[idx].x(), P[idx].y(), P[idx].z()));
            uv[idx] = UV(uu, vv);
        }
    }

    auto VERT = [NU](int u, int v) { return static_cast<size_t>(v) * NU + u; };
    for (int v = 0; v < NV - 1; ++v) {
        for (int u = 0; u < NU - 1; ++u) {
            const size_t a = VERT(u, v), b = VERT(u + 1, v),
                         c = VERT(u + 1, v + 1), d = VERT(u, v + 1);
            TriangleDesc t1;
            t1.v0 = m_toWorld * P[a]; t1.v1 = m_toWorld * P[b]; t1.v2 = m_toWorld * P[c];
            t1.n0 = m_toWorld * N[a]; t1.n1 = m_toWorld * N[b]; t1.n2 = m_toWorld * N[c];
            t1.uv0 = uv[a]; t1.uv1 = uv[b]; t1.uv2 = uv[c];
            out.push_back(t1);

            TriangleDesc t2;
            t2.v0 = m_toWorld * P[a]; t2.v1 = m_toWorld * P[c]; t2.v2 = m_toWorld * P[d];
            t2.n0 = m_toWorld * N[a]; t2.n1 = m_toWorld * N[c]; t2.n2 = m_toWorld * N[d];
            t2.uv0 = uv[a]; t2.uv1 = uv[c]; t2.uv2 = uv[d];
            out.push_back(t2);
        }
    }
}

std::shared_ptr<Shape> SphereShape::CreateShape(const PluginContext &ctx) {
    const ParamSet &p = *ctx.params;
    const float radius = p.FindOneFloat("radius", 1.f);
    const float zmin = p.FindOneFloat("zmin", -radius);
    const float zmax = p.FindOneFloat("zmax", radius);
    const float phimax = p.FindOneFloat("phimax", 360.f);
    const int nu = p.FindOneInt("phisegments", 64);
    const int nv = p.FindOneInt("thetasegments", 32);
    return std::make_shared<SphereShape>(ctx.transform, radius, zmin, zmax,
                                         phimax, nu, nv);
}

LUX2_REGISTER_SHAPE(SphereShape, "sphere");

} // namespace lux2
