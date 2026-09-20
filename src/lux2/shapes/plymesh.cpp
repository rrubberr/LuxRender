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

#include "shapes/plymesh.h"

#include "core/dynload.h"
#include "core/paramset.h"
#include "core/plymesh.h"
#include "core/register.h"

namespace lux2 {

PlyMeshShape::PlyMeshShape(const Transform &toWorld, std::vector<Point3f> P,
                           std::vector<Normal3f> N, std::vector<UV> uv,
                           std::vector<std::array<int, 3>> tris)
    : m_toWorld(toWorld), m_P(std::move(P)), m_N(std::move(N)),
      m_uv(std::move(uv)), m_tris(std::move(tris)) {
    for (const Point3f &p : m_P)
        m_objBound = Union(m_objBound, p);
}

BBox PlyMeshShape::WorldBound() const {
    if (!m_objBound.IsValid())
        return BBox();
    BBox wb;
    for (int i = 0; i < 8; ++i) {
        const Point3f c((i & 1) ? m_objBound.pMax.x() : m_objBound.pMin.x(),
                        (i & 2) ? m_objBound.pMax.y() : m_objBound.pMin.y(),
                        (i & 4) ? m_objBound.pMax.z() : m_objBound.pMin.z());
        wb = Union(wb, m_toWorld * c);
    }
    return wb;
}

void PlyMeshShape::Tessellate(const Transform & /*worldToCamera*/,
                              std::vector<TriangleDesc> &out) const {
    for (const auto &t : m_tris) {
        const int i0 = t[0], i1 = t[1], i2 = t[2];
        TriangleDesc td;
        td.v0 = m_toWorld * m_P[i0];
        td.v1 = m_toWorld * m_P[i1];
        td.v2 = m_toWorld * m_P[i2];
        td.n0 = m_toWorld * m_N[i0];
        td.n1 = m_toWorld * m_N[i1];
        td.n2 = m_toWorld * m_N[i2];
        td.uv0 = m_uv[i0];
        td.uv1 = m_uv[i1];
        td.uv2 = m_uv[i2];
        out.push_back(td);
    }
}

std::shared_ptr<Shape> PlyMeshShape::CreateShape(const PluginContext &ctx) {
    const ParamSet &p = *ctx.params;
    const std::string filename = p.FindOneString("filename", "none");

    std::vector<Point3f> P;
    std::vector<Normal3f> N;
    std::vector<UV> uv;
    std::vector<std::array<int, 3>> tris;
    if (!ReadPlyGeometry(filename, P, N, uv, tris)) {
        LOG(LUX_ERROR, LUX_BADFILE)
            << "plymesh: failed to read '" << filename << "'";
        return nullptr;
    }
    return std::make_shared<PlyMeshShape>(ctx.transform, std::move(P),
                                          std::move(N), std::move(uv),
                                          std::move(tris));
}

LUX2_REGISTER_SHAPE(PlyMeshShape, "plymesh");

} // namespace lux2
