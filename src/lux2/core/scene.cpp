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

#include "core/scene.h"

namespace lux2 {

void Scene::Commit(SceneDescription& desc) {
    m_summary = Summary{};

    // Merge named materials so bindings resolve against the full table.
    for (const auto& kv : desc.namedMaterials)
        m_summary.namedMaterialCount++;

    for (const auto& shape : desc.shapes) {
        m_summary.shapeCount++;

        // Validate the material binding resolves.
        if (shape.material.valid()) {
            if (shape.material.isNamed) {
                if (desc.namedMaterials.find(shape.material.namedRef) ==
                    desc.namedMaterials.end()) {
                    LOG(LUX_ERROR) << "Shape references unknown named material '"
                                   << shape.material.namedRef << "'";
                }
            }
        }

        if (shape.isAreaLight) {
            m_summary.areaLightShapeCount++;
            if (shape.areaLightName.empty())
                LOG(LUX_ERROR) << "Area-light shape has no light plugin name";
        }
    }

    for (const auto& light : desc.lights) {
        m_summary.lightCount++;
        if (light.name.empty())
            LOG(LUX_ERROR) << "Light source has no plugin name";
    }

    m_committed = true;
}

} // namespace lux2
