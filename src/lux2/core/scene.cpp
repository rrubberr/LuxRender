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

#include "core/dynload.h"
#include "core/embree2.h"
#include "core/shape.h"

#include <map>
#include <string>

namespace lux2 {

Scene::Scene() = default;
Scene::~Scene() = default;

void Scene::Commit(SceneDescription& desc) {
    m_summary = Summary{};
    desc.meshes.clear();

    // Merge named materials so bindings resolve against the full table.
    for (const auto& kv : desc.namedMaterials)
        m_summary.namedMaterialCount++;

    // Dedup materials into a stable integer id per distinct binding.
    std::map<std::string, std::uint32_t> matIds;
    auto resolveMatID = [&](const MaterialBinding& m) -> std::uint32_t {
        if (!m.valid())
            return 0;  // default material
        const std::string key =
            m.isNamed ? ("#" + m.namedRef) : ("!" + m.pluginName);
        auto it = matIds.find(key);
        if (it != matIds.end())
            return it->second;
        const std::uint32_t id = static_cast<std::uint32_t>(matIds.size());
        matIds.emplace(key, id);
        return id;
    };

    auto &shapeRegistry = DynamicLoader::registeredShapes();
    std::int32_t nextAreaLightID = 0;

    for (const auto& shape : desc.shapes) {
        m_summary.shapeCount++;

        // Validate the material binding resolves.
        if (shape.material.valid()) {
            if (shape.material.isNamed) {
                if (desc.namedMaterials.find(shape.material.namedRef) ==
                    desc.namedMaterials.end()) {
                    LOG(LUX_ERROR, LUX_BADHANDLE) << "Shape references unknown named material '"
                                   << shape.material.namedRef << "'";
                }
            }
        }

        std::int32_t lightID = -1;
        if (shape.isAreaLight) {
            m_summary.areaLightShapeCount++;
            if (shape.areaLightName.empty())
                LOG(LUX_ERROR, LUX_SYNTAX) << "Area-light shape has no light plugin name";
            lightID = nextAreaLightID++;
        }

        // Instantiate the shape plugin and tessellate to world space triangles.
        auto it = shapeRegistry.find(shape.name);
        if (it == shapeRegistry.end()) {
            LOG(LUX_ERROR, LUX_BADHANDLE)
                << "No shape plugin registered as '" << shape.name << "'";
            desc.meshes.emplace_back();  // keep geomID == shape index
            continue;
        }

        PluginContext pctx;
        pctx.params = const_cast<ParamSet*>(&shape.params);
        pctx.transform = shape.toWorld;
        std::shared_ptr<Shape> s = it->second(pctx);
        if (!s) {
            LOG(LUX_ERROR, LUX_SYNTAX)
                << "Shape plugin '" << shape.name << "' failed to create";
            desc.meshes.emplace_back();
            continue;
        }

        const std::uint32_t matID = resolveMatID(shape.material);
        MeshDesc md;
        s->Tessellate(Transform(), md.tris);
        for (auto& td : md.tris) {
            td.matID = matID;
            td.lightID = lightID;
        }
        md.bound = s->WorldBound();
        m_summary.triangleCount += static_cast<int>(md.tris.size());
        desc.meshes.push_back(std::move(md));
    }

    for (const auto& light : desc.lights) {
        m_summary.lightCount++;
        if (light.name.empty())
            LOG(LUX_ERROR, LUX_SYNTAX) << "Light source has no plugin name";
    }

    // Build the Embree accelerator from the tessellated meshes.
    m_embree = std::make_unique<EmbreeScene>();
    m_embree->BuildFromMeshes(desc.meshes);
    m_worldBound = m_embree->WorldBound();

    m_committed = true;
}

} // namespace lux2
