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
#include "core/light.h"
#include "core/shape.h"
#include "core/integrator.h"
#include "core/renderer.h"
#include "core/sampler.h"
#include "core/material.h"
#include "core/paramset.h"
#include "core/graphicsstate.h"

#include "core/bbox.h"
#include "core/transform.h"

#include <vector>
#include <map>
#include <string>
#include <memory>

namespace lux2 {

// -----------------------------------------------------------------------
// Parse-time descriptors
// -----------------------------------------------------------------------

// A shape as recorded at parse time: plugin name, parameters, the object-to-world
// transform in effect, and the material/area-light bindings.
struct ShapeDesc {
    std::string name;        // Shape plugin name ("plymesh", "sphere", etc.).
    ParamSet params;         // Shape parameters.
    Transform toWorld;       // Object-to-world at declaration time.

    // Material binding (invalid resolves "default material").
    MaterialBinding material;

    // Area light bound to this shape.
    bool isAreaLight = false;
    std::string areaLightName;   // Light plugin name ("area").
    ParamSet areaLightParams;
    std::string lightGroup;
};

// A non-area light source as recorded at parse time.
struct LightDesc {
    std::string name;        // Light plugin name ("sun", "sky", etc).
    ParamSet params;
    Transform toWorld;
    std::string lightGroup;
};

// -----------------------------------------------------------------------
// Commit-output descriptors
// -----------------------------------------------------------------------

// A tessellated, world-space mesh produced from a ShapeDesc.
struct MeshDesc {
    std::vector<TriangleDesc> tris;     // World-space triangles.
    BBox bound;                         // World-space bound.
};

// -----------------------------------------------------------------------
// SceneDescription
// -----------------------------------------------------------------------

// The scene builder.
struct SceneDescription {
    // Plugin names and their parameters.
    std::string rendererName;   ParamSet rendererParams;
    std::string samplerName;    ParamSet samplerParams;
    std::string filmName;       ParamSet filmParams;
    std::string filterName;     ParamSet filterParams;
    std::string cameraName;     ParamSet cameraParams;
    std::string surfIntName;    ParamSet surfIntParams;
    std::string volIntName;     ParamSet volIntParams;

    // Parse-time records.
    std::vector<ShapeDesc> shapes;
    std::vector<LightDesc> lights;

    // Named materials: name -> parameters (the "type" string selects the
    // plugin).
    std::map<std::string, ParamSet> namedMaterials;

    // Commit-output (P3): tessellated meshes and resolved materials.
    std::vector<MeshDesc> meshes;
    std::vector<std::shared_ptr<Material>> materials;

    // Camera world transform.
    Transform cameraTransform;
};

// -----------------------------------------------------------------------
// Scene
// -----------------------------------------------------------------------

// Built from a SceneDescription at WorldEnd.
class Scene {
public:
    Scene() = default;
    ~Scene() = default;

    // Build the Embree scene. Called at WorldEnd.
    void Commit(SceneDescription& desc);

    const Camera& GetCamera() const { return *m_camera; }
    Film& GetFilm() { return *m_film; }
    SurfaceIntegrator& GetSurfaceIntegrator() { return *m_surfaceIntegrator; }
    VolumeIntegrator& GetVolumeIntegrator() { return *m_volumeIntegrator; }
    Renderer& GetRenderer() { return *m_renderer; }
    Sampler& GetSampler() { return *m_sampler; }

    const std::vector<std::shared_ptr<Light>>& GetLights() const { return m_lights; }

    BBox WorldBound() const { return m_worldBound; }

   // True after a successful Commit.
    bool IsCommitted() const { return m_committed; }

    // Tally produced by Commit (used by the B.17 summary/check until P3 adds
    // real tessellation).
    struct Summary {
        int shapeCount = 0;
        int areaLightShapeCount = 0;
        int lightCount = 0;
        int namedMaterialCount = 0;
    };
    const Summary& GetSummary() const { return m_summary; }

private:
    std::shared_ptr<Camera> m_camera;
    std::shared_ptr<Film> m_film;
    std::shared_ptr<SurfaceIntegrator> m_surfaceIntegrator;
    std::shared_ptr<VolumeIntegrator> m_volumeIntegrator;
    std::shared_ptr<Renderer> m_renderer;
    std::shared_ptr<Sampler> m_sampler;
    std::vector<std::shared_ptr<Light>> m_lights;
    BBox m_worldBound;
    Summary m_summary;
    bool m_committed = false;
};

} // namespace lux2

#endif // LUX2_SCENE_H
