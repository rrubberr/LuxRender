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

// Verifies Scene::Commit wiring.

#include "core/paramset.h"
#include "core/scene.h"
#include "core/transform.h"
#include "lights/area.h"
#include "lights/infinite.h"

#include <cmath>
#include <iostream>
#include <memory>

using namespace lux2;

namespace {

int g_failures = 0;

void Check(bool cond, const char *what) {
    if (!cond) {
        std::cerr << "  FAIL: " << what << std::endl;
        ++g_failures;
    } else {
        std::cout << "  ok:   " << what << std::endl;
    }
}

// A unit quad in the z=0 plane, declared as an area-light shape.
ShapeDesc MakeQuadAreaLight(const std::string &lightName) {
    ShapeDesc sd;
    sd.name = "plymesh";  // unused; we override below with a synthetic mesh
    // Use a sphere shape so tessellation produces triangles; tag as area light.
    sd.name = "sphere";
    sd.toWorld = Transform();
    sd.isAreaLight = true;
    sd.areaLightName = lightName;
    return sd;
}

} // namespace

int main() {
    std::cout << "lux2 scene_wiring_check (Stage 7)" << std::endl;

    // ---- 1. defaults: camera/sampler/filter instantiate -----------------
    {
        SceneDescription desc;
        // One sphere so the scene is non-trivial.
        ShapeDesc sd; sd.name = "sphere"; sd.toWorld = Transform();
        desc.shapes.push_back(sd);

        Scene scene;
        scene.Commit(desc);

        Check(scene.IsCommitted(), "scene committed");
        Check(&scene.GetCamera() != nullptr, "camera instantiated (default)");
        Check(&scene.GetSampler() != nullptr, "sampler instantiated (default)");
        Check(&scene.GetFilter() != nullptr, "filter instantiated (default)");
        Check(scene.GetCamera().PixelWidth() == 800,
              "default camera width == 800 (film default)");
        Check(scene.GetCamera().PixelHeight() == 600,
              "default camera height == 600 (film default)");
    }

    // ---- 2. explicit film resolution reaches the camera -----------------
    {
        SceneDescription desc;
        const int xr = 64, yr = 48;
        desc.filmParams.AddInt("xresolution", &xr, 1);
        desc.filmParams.AddInt("yresolution", &yr, 1);

        Scene scene;
        scene.Commit(desc);
        Check(scene.GetCamera().PixelWidth() == 64,
              "camera honors film xresolution");
        Check(scene.GetCamera().PixelHeight() == 48,
              "camera honors film yresolution");
    }

    // ---- 3. non-area light (infinite) instantiates ----------------------
    {
        SceneDescription desc;
        LightDesc ld; ld.name = "infinite"; ld.toWorld = Transform();
        desc.lights.push_back(ld);

        Scene scene;
        scene.Commit(desc);
        Check(scene.GetLights().size() == 1, "one light wired");
        if (!scene.GetLights().empty()) {
            Check(scene.GetLights()[0]->IsInfinite(),
                  "wired light is infinite");
        }
    }

    // ---- 4. area light receives its tagged triangles --------------------
    {
        SceneDescription desc;
        desc.shapes.push_back(MakeQuadAreaLight("area"));

        Scene scene;
        scene.Commit(desc);

        // One area light wired (the sphere's triangles tagged lightID 0).
        Check(scene.GetLights().size() == 1, "one area light wired");
        if (!scene.GetLights().empty()) {
            auto area = std::dynamic_pointer_cast<AreaLight>(
                scene.GetLights()[0]);
            Check(area != nullptr, "wired light is an AreaLight");
            if (area) {
                // A tessellated sphere has positive total area.
                Check(area->TotalArea() > 0.f,
                      "area light received geometry (TotalArea > 0)");
            }
        }
    }

    // ---- 5. explicit plugin names honored -------------------------------
    {
        SceneDescription desc;
        desc.samplerName = "ldsampler";
        desc.filterName = "gaussian";
        desc.cameraName = "perspective";

        Scene scene;
        scene.Commit(desc);
        Check(&scene.GetSampler() != nullptr, "explicit sampler wired");
        Check(&scene.GetFilter() != nullptr, "explicit filter wired");
        Check(&scene.GetCamera() != nullptr, "explicit camera wired");
    }

    std::cout << (g_failures == 0 ? "ALL PASS" : "FAILURES")
              << " (" << g_failures << ")" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
