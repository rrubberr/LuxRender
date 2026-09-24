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
#include "core/texture.h"
#include "lights/area.h"
#include "materials/null.h"

#include <map>
#include <string>
#include <vector>

namespace lux2
{

    Scene::Scene() = default;
    Scene::~Scene() = default;

    namespace
    {

        // Resolve a named texture from desc.textures.
        template <typename TexPtr, typename Registry>
        TexPtr ResolveNamedTexture(const std::string &name,
                                   const std::map<std::string, TextureDesc> &texDescs,
                                   Registry &registry)
        {
            if (name.empty())
                return nullptr;
            auto dit = texDescs.find(name);
            if (dit == texDescs.end())
                return nullptr;
            auto rit = registry.find(dit->second.pluginName);
            if (rit == registry.end())
                return nullptr;
            PluginContext pctx;
            pctx.params = const_cast<ParamSet *>(&dit->second.params);
            return rit->second(pctx);
        }

    } // namespace

    void Scene::Commit(SceneDescription &desc)
    {
        m_summary = Summary{};
        desc.meshes.clear();
        desc.materials.clear();
        m_bsdfTable.ptrs.clear();

        // Build texture tables from desc.textures.
        std::map<std::string, std::shared_ptr<ColorTexture>> colorTexTable;
        std::map<std::string, std::shared_ptr<FloatTexture>> floatTexTable;
        std::map<std::string, std::shared_ptr<FresnelTexture>> fresnelTexTable;

        auto &colorReg = DynamicLoader::registeredColorTextures();
        auto &floatReg = DynamicLoader::registeredFloatTextures();
        auto &fresnelReg = DynamicLoader::registeredFresnelTextures();

        for (const auto &kv : desc.textures)
        {
            const std::string &name = kv.first;
            const TextureDesc &td = kv.second;
            if (td.textureType == "color")
            {
                auto tex = ResolveNamedTexture<std::shared_ptr<ColorTexture>>(
                    name, desc.textures, colorReg);
                if (tex)
                    colorTexTable[name] = std::move(tex);
            }
            else if (td.textureType == "float")
            {
                auto tex = ResolveNamedTexture<std::shared_ptr<FloatTexture>>(
                    name, desc.textures, floatReg);
                if (tex)
                    floatTexTable[name] = std::move(tex);
            }
            else if (td.textureType == "fresnel")
            {
                auto tex = ResolveNamedTexture<std::shared_ptr<FresnelTexture>>(
                    name, desc.textures, fresnelReg);
                if (tex)
                    fresnelTexTable[name] = std::move(tex);
            }
        }

        // Material instantiation.
        auto &matReg = DynamicLoader::registeredMaterials();

        // Instantiate a material from its ParamSet.
        auto instantiateMaterial = [&](const ParamSet &params)
            -> std::shared_ptr<Material>
        {
            const std::string type = params.FindOneString("type", "null");
            auto it = matReg.find(type);
            if (it == matReg.end())
            {
                LOG(LUX_ERROR, LUX_BADHANDLE)
                    << "No material plugin registered as '" << type << "'";
                return std::make_shared<NullMaterial>();
            }
            PluginContext pctx;
            pctx.params = const_cast<ParamSet *>(&params);
            pctx.colorTextures = &colorTexTable;
            pctx.floatTextures = &floatTexTable;
            pctx.fresnelTextures = &fresnelTexTable;
            auto mat = it->second(pctx);
            if (!mat)
            {
                LOG(LUX_ERROR, LUX_SYNTAX)
                    << "Material plugin '" << type << "' returned null";
                return std::make_shared<NullMaterial>();
            }
            return mat;
        };

        // Default to null material for unbound shapes.
        {
            auto defaultMat = std::make_shared<NullMaterial>();
            desc.materials.push_back(defaultMat);
            m_bsdfTable.ptrs.push_back(defaultMat->GetBSDF(DifferentialGeometryP{}));
        }

        // Instantiate named materials.
        for (const auto &kv : desc.namedMaterials)
        {
            m_summary.namedMaterialCount++;
            auto mat = instantiateMaterial(kv.second);
            const std::uint32_t id = static_cast<std::uint32_t>(desc.materials.size());
            desc.materials.push_back(mat);
            m_bsdfTable.ptrs.push_back(mat->GetBSDF(DifferentialGeometryP{}));
        }

        // Dedup map: binding key -> material id.
        std::map<std::string, std::uint32_t> matIds;
        // Pre-populate named materials.
        {
            std::uint32_t idx = 1;
            for (const auto &kv : desc.namedMaterials)
            {
                matIds["#" + kv.first] = idx++;
            }
        }

        auto resolveMatID = [&](const MaterialBinding &m) -> std::uint32_t
        {
            if (!m.valid())
                return 0;
            if (m.isNamed)
            {
                auto it = matIds.find("#" + m.namedRef);
                if (it != matIds.end())
                    return it->second;
                LOG(LUX_ERROR, LUX_BADHANDLE)
                    << "Shape references unknown named material '"
                    << m.namedRef << "'";
                return 0;
            }
            // Dedup by plugin name.
            const std::string key = "!" + m.pluginName;
            auto it = matIds.find(key);
            if (it != matIds.end())
                return it->second;
            auto mat = instantiateMaterial(m.params);
            const std::uint32_t id = static_cast<std::uint32_t>(desc.materials.size());
            desc.materials.push_back(mat);
            m_bsdfTable.ptrs.push_back(mat->GetBSDF(DifferentialGeometryP{}));
            matIds[key] = id;
            return id;
        };

        auto &shapeRegistry = DynamicLoader::registeredShapes();
        std::int32_t nextAreaLightID = 0;

        // Area light sources collected during tessellation.
        struct AreaLightRecord
        {
            std::string name;
            const ParamSet *params;
        };
        std::vector<AreaLightRecord> areaLightRecords;

        for (const auto &shape : desc.shapes)
        {
            m_summary.shapeCount++;

            std::int32_t lightID = -1;
            if (shape.isAreaLight)
            {
                m_summary.areaLightShapeCount++;
                if (shape.areaLightName.empty())
                    LOG(LUX_ERROR, LUX_SYNTAX) << "Area-light shape has no light plugin name";
                lightID = nextAreaLightID++;
                areaLightRecords.push_back({shape.areaLightName,
                                            &shape.areaLightParams});
            }

            // Instantiate the shape plugin and tessellate to world space triangles.
            auto it = shapeRegistry.find(shape.name);
            if (it == shapeRegistry.end())
            {
                LOG(LUX_ERROR, LUX_BADHANDLE)
                    << "No shape plugin registered as '" << shape.name << "'";
                desc.meshes.emplace_back();
                continue;
            }

            PluginContext pctx;
            pctx.params = const_cast<ParamSet *>(&shape.params);
            pctx.transform = shape.toWorld;
            std::shared_ptr<Shape> s = it->second(pctx);
            if (!s)
            {
                LOG(LUX_ERROR, LUX_SYNTAX)
                    << "Shape plugin '" << shape.name << "' failed to create";
                desc.meshes.emplace_back();
                continue;
            }

            const std::uint32_t matID = resolveMatID(shape.material);
            MeshDesc md;
            s->Tessellate(Transform(), md.tris);
            for (auto &td : md.tris)
            {
                td.matID = matID;
                td.lightID = lightID;
            }
            md.bound = s->WorldBound();
            m_summary.triangleCount += static_cast<int>(md.tris.size());
            desc.meshes.push_back(std::move(md));
        }

        // Build the Embree accelerator from the tessellated meshes.
        m_embree = std::make_unique<EmbreeScene>();
        m_embree->BuildFromMeshes(desc.meshes);
        m_worldBound = m_embree->WorldBound();

        // Light instantiation.
        auto &lightReg = DynamicLoader::registeredLights();

        // Non-area lights are instantiated directly from their descriptors.
        for (const auto &light : desc.lights)
        {
            m_summary.lightCount++;
            if (light.name.empty())
            {
                LOG(LUX_ERROR, LUX_SYNTAX) << "Light source has no plugin name";
                continue;
            }
            auto it = lightReg.find(light.name);
            if (it == lightReg.end())
            {
                LOG(LUX_ERROR, LUX_BADHANDLE)
                    << "No light plugin registered as '" << light.name << "'";
                continue;
            }
            PluginContext pctx;
            pctx.params = const_cast<ParamSet *>(&light.params);
            pctx.transform = light.toWorld;
            pctx.colorTextures = &colorTexTable;
            pctx.floatTextures = &floatTexTable;
            auto l = it->second(pctx);
            if (l)
                m_lights.push_back(std::move(l));
        }

        // Instantiate the "area" plugin per lightID, then gather the
        // triangles tagged with that lightID and bind them.
        for (std::size_t i = 0; i < areaLightRecords.size(); ++i)
        {
            const AreaLightRecord &rec = areaLightRecords[i];
            auto it = lightReg.find(rec.name.empty() ? "area" : rec.name);
            if (it == lightReg.end())
            {
                LOG(LUX_ERROR, LUX_BADHANDLE)
                    << "No area-light plugin registered as '" << rec.name << "'";
                continue;
            }
            PluginContext pctx;
            pctx.params = const_cast<ParamSet *>(rec.params);
            pctx.colorTextures = &colorTexTable;
            pctx.floatTextures = &floatTexTable;
            auto l = it->second(pctx);
            if (!l)
            {
                LOG(LUX_ERROR, LUX_SYNTAX)
                    << "Area-light plugin '" << rec.name << "' returned null";
                continue;
            }

            // Gather triangles whose lightID matches this area light.
            std::vector<TriangleDesc> tris;
            for (const auto &md : desc.meshes)
                for (const auto &td : md.tris)
                    if (td.lightID == static_cast<std::int32_t>(i))
                        tris.push_back(td);

            auto area = std::dynamic_pointer_cast<AreaLight>(l);
            if (area)
                area->BindGeometry(tris);
            m_lights.push_back(std::move(l));
        }

        // Sampler/filter/camera.
        {
            auto &samplerReg = DynamicLoader::registeredSamplers();
            const std::string sname =
                desc.samplerName.empty() ? "ldsampler" : desc.samplerName;
            auto it = samplerReg.find(sname);
            if (it != samplerReg.end())
            {
                PluginContext pctx;
                pctx.params = &desc.samplerParams;
                m_sampler = it->second(pctx);
            }
            else
            {
                LOG(LUX_ERROR, LUX_BADHANDLE)
                    << "No sampler plugin registered as '" << sname << "'";
            }
        }

        {
            auto &filterReg = DynamicLoader::registeredFilters();
            const std::string fname =
                desc.filterName.empty() ? "gaussian" : desc.filterName;
            auto it = filterReg.find(fname);
            if (it != filterReg.end())
            {
                PluginContext pctx;
                pctx.params = &desc.filterParams;
                m_filter = it->second(pctx);
            }
            else
            {
                LOG(LUX_ERROR, LUX_BADHANDLE)
                    << "No filter plugin registered as '" << fname << "'";
            }
        }

        {
            auto &cameraReg = DynamicLoader::registeredCameras();
            const std::string cname =
                desc.cameraName.empty() ? "perspective" : desc.cameraName;
            auto it = cameraReg.find(cname);
            if (it != cameraReg.end())
            {
                PluginContext pctx;
                pctx.params = &desc.cameraParams;
                pctx.transform = desc.cameraTransform;
                pctx.filmParams = &desc.filmParams;
                m_camera = it->second(pctx);
            }
            else
            {
                LOG(LUX_ERROR, LUX_BADHANDLE)
                    << "No camera plugin registered as '" << cname << "'";
            }
        }

        m_committed = true;
    }

} // namespace lux2
