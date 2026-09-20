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

#ifndef LUX2_SHAPE_SPHERE_H
#define LUX2_SHAPE_SPHERE_H

#include "core/shape.h"
#include "core/bbox.h"
#include "core/transform.h"

#include <memory>

namespace lux2 {

class PluginContext;

// Analytic sphere tessellated to triangles on a lat/long grid.
class SphereShape : public Shape {
public:
    SphereShape(const Transform &toWorld, float radius, float zMin,
                float zMax, float phiMaxDeg, int phiSegments,
                int thetaSegments);

    void Tessellate(const Transform &worldToCamera,
                    std::vector<TriangleDesc> &out) const override;
    BBox WorldBound() const override;

    static std::shared_ptr<Shape> CreateShape(const PluginContext &ctx);

private:
    Transform m_toWorld;
    float m_radius;
    float m_zMin, m_zMax;
    float m_thetaMin, m_thetaMax; // radians
    float m_phiMax;               // radians
    int m_nu, m_nv;               // grid resolution (phi, theta)
};

} // namespace lux2

#endif // LUX2_SHAPE_SPHERE_H
