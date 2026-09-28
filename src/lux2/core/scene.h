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

#ifndef LUX2_SCENE_H
#define LUX2_SCENE_H

#include "core/camera.h"
#include "core/film.h"
#include "core/filter.h"
#include "core/light.h"
#include "core/shape.h"
#include "core/integrator.h"
#include "core/renderer.h"
#include "core/sampler.h"
#include "core/material.h"
#include "core/paramset.h"
#include "core/graphicsstate.h"
#include "core/bsdfptr_table.h"

#include "core/bbox.h"
#include "core/transform.h"

#include <vector>
#include <map>
#include <string>
#include <memory>

namespace lux2
{

    class EmbreeScene;

    // -----------------------------------------------------------------------
    // Parse-time descriptors
    // -----------------------------------------------------------------------

    // Plugin name, parameters, the object-to-world
    // transform in effect, and material/area-light bindings.
    struct ShapeDesc
    {
        std::string name;  // shape plugin name
        ParamSet params;   // shape parameters
        Transform toWorld; // object-to-world at declaration time

        // Material binding.
        MaterialBinding material;

        // Area light bound to this shape.
        bool isAreaLight = false;
        std::string areaLightName; // light plugin name.
        ParamSet areaLightParams;
        std::string lightGroup;
    };

    // A non-area light source as recorded at parse time.
    struct LightDesc
    {
        std::string name; // light plugin name
        ParamSet params;
        Transform toWorld;
        std::string lightGroup;
    };

    // -----------------------------------------------------------------------
    // Commit-output descriptors
    // -----------------------------------------------------------------------

    // A tessellated, world-space mesh produced from a ShapeDesc.
    struct MeshDesc
    {
        std::vector<TriangleDesc> tris; // world space triangles
        BBox bound;                     // world space bound
    };

    // -----------------------------------------------------------------------
    // SceneDescription
    // -----------------------------------------------------------------------

    // The scene builder.
    struct SceneDescription
    {
        // Plugin names and their parameters.
        std::string rendererName;
        ParamSet rendererParams;
        std::string samplerName;
        ParamSet samplerParams;
        std::string filmName;
        ParamSet filmParams;
        std::string filterName;
        ParamSet filterParams;
        std::string cameraName;
        ParamSet cameraParams;
        std::string surfIntName;
        ParamSet surfIntParams;
        std::string volIntName;
        ParamSet volIntParams;

        // Parse records.
        std::vector<ShapeDesc> shapes;
        std::vector<LightDesc> lights;

        // Named materials name -> parameters.
        std::map<std::string, ParamSet> namedMaterials;

        // Textures declared in the world block: name -> descriptor.
        std::map<std::string, TextureDesc> textures;

        // Tessellated meshes and resolved materials.
        std::vector<MeshDesc> meshes;
        std::vector<std::shared_ptr<Material>> materials;

        // Camera world transform.
        Transform cameraTransform;
    };

    // -----------------------------------------------------------------------
    // Scene
    // -----------------------------------------------------------------------

    // Built from a SceneDescription at WorldEnd.
    class Scene
    {
    public:
        Scene();
        ~Scene();

        // Build the Embree scene. Called at WorldEnd.
        void Commit(SceneDescription &desc);

        const Camera &GetCamera() const { return *m_camera; }
        Film &GetFilm() const { return *m_film; }
        SurfaceIntegrator &GetSurfaceIntegrator() { return *m_surfaceIntegrator; }
        VolumeIntegrator &GetVolumeIntegrator() { return *m_volumeIntegrator; }
        Renderer &GetRenderer() { return *m_renderer; }
        Sampler &GetSampler() { return *m_sampler; }
        Filter &GetFilter() { return *m_filter; }

        const std::vector<std::shared_ptr<Light>> &GetLights() const { return m_lights; }

        // Area lights indexed by lightID.
        const Light *GetAreaLight(std::int32_t id) const
        {
            if (id < 0 || id >= static_cast<std::int32_t>(m_areaLights.size()))
                return nullptr;
            return m_areaLights[id].get();
        }
        int AreaLightCount() const
        {
            return static_cast<int>(m_areaLights.size());
        }

        BBox WorldBound() const { return m_worldBound; }

        // The Embree accelerator built at Commit.
        const EmbreeScene *GetEmbree() const { return m_embree.get(); }

        // BSDF pointer table.
        const BsdfPtrTable &GetBsdfTable() const { return m_bsdfTable; }

        // True after a successful Commit.
        bool IsCommitted() const { return m_committed; }

        // Tally produced by Commit.
        struct Summary
        {
            int shapeCount = 0;
            int areaLightShapeCount = 0;
            int lightCount = 0;
            int namedMaterialCount = 0;
            int triangleCount = 0; // total tessellated triangles
        };
        const Summary &GetSummary() const { return m_summary; }

    private:
        std::shared_ptr<Camera> m_camera;
        std::shared_ptr<Film> m_film;
        std::shared_ptr<SurfaceIntegrator> m_surfaceIntegrator;
        std::shared_ptr<VolumeIntegrator> m_volumeIntegrator;
        std::shared_ptr<Renderer> m_renderer;
        std::shared_ptr<Sampler> m_sampler;
        std::shared_ptr<Filter> m_filter;
        std::vector<std::shared_ptr<Light>> m_lights;
        std::vector<std::shared_ptr<Light>> m_areaLights; // indexed by lightID
        std::vector<std::shared_ptr<Material>> m_materials;
        BBox m_worldBound;
        Summary m_summary;
        bool m_committed = false;
        std::unique_ptr<EmbreeScene> m_embree;
        BsdfPtrTable m_bsdfTable;
    };

} // namespace lux2

#endif // LUX2_SCENE_H
