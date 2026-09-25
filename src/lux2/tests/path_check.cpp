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

// Stage 1: NullFilm + PathIntegrator skeleton. Verifies the integrator is
// registered, constructs via the DynamicLoader, and one RenderPass over a
// single tile terminates and produces finite (non-NaN) radiance. No image.

#include "core/dynload.h"
#include "core/paramset.h"
#include "core/scene.h"
#include "core/tilequeue.h"
#include "core/transform.h"
#include "film/null.h"
#include "integrators/path.h"

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

} // namespace

int main() {
    std::cout << "lux2 path_check (Stage 1)" << std::endl;

    // ---- 1. both plugins are registered --------------------------------
    {
        auto &filmReg = DynamicLoader::registeredFilms();
        auto &siReg = DynamicLoader::registeredSurfaceIntegrators();
        Check(filmReg.count("null") == 1, "NullFilm registered as 'null'");
        Check(siReg.count("path") == 1, "PathIntegrator registered as 'path'");
    }

    // ---- 2. construct the integrator via the registry ------------------
    std::shared_ptr<SurfaceIntegrator> integrator;
    {
        auto &siReg = DynamicLoader::registeredSurfaceIntegrators();
        auto it = siReg.find("path");
        if (it != siReg.end()) {
            ParamSet params;
            const int md = 6;
            const float rr = 0.5f;
            params.AddInt("maxdepth", &md, 1);
            params.AddFloat("rrcontinueprob", &rr, 1);
            PluginContext pctx;
            pctx.params = &params;
            integrator = it->second(pctx);
        }
        Check(integrator != nullptr, "PathIntegrator constructed via registry");
        if (integrator) {
            Check(integrator->PassCount() == 1, "PassCount() == 1");
            auto *path = dynamic_cast<PathIntegrator *>(integrator.get());
            Check(path != nullptr, "registry instance is a PathIntegrator");
            if (path) {
                Check(path->MaxDepth() == 6, "maxdepth param parsed (6)");
                Check(path->RRContinueProb() == 0.5f,
                      "rrcontinueprob param parsed (0.5)");
                Check(path->IncludeEnvironment() == true,
                      "includeenvironment defaults true");
                Check(path->DirectLightSampling() == true,
                      "directlightsampling defaults true");
            }
        }
    }

    // ---- 3. run one RenderPass over a single tile ----------------------
    {
        // A committed scene: one sphere lit by an area light.
        SceneDescription desc;
        const int xr = 16, yr = 16;
        desc.filmParams.AddInt("xresolution", &xr, 1);
        desc.filmParams.AddInt("yresolution", &yr, 1);

        ShapeDesc sd;
        sd.name = "sphere";
        sd.toWorld = Transform();
        sd.isAreaLight = true;
        sd.areaLightName = "area";
        desc.shapes.push_back(sd);

        Scene scene;
        scene.Commit(desc);
        Check(scene.IsCommitted(), "scene committed");

        // The scene wires a NullFilm + PathIntegrator by default.
        Check(dynamic_cast<NullFilm *>(&scene.GetFilm()) != nullptr,
              "scene film is a NullFilm");

        if (integrator) {
            integrator->Start(scene);

            TileQueue tiles;
            Tile t;
            t.x0 = 0; t.y0 = 0; t.x1 = xr; t.y1 = yr;
            tiles.Push(t);

            // Drive one pass; must terminate (queue drains) without hanging.
            integrator->RenderPass(scene, tiles, scene.GetSampler(), 0);

            Check(tiles.Empty(), "tile queue drained (loop terminated)");

            auto *film = dynamic_cast<NullFilm *>(&scene.GetFilm());
            if (film) {
                const double mean = film->MeanLuminance();
                Check(std::isfinite(mean), "accumulated radiance is finite");
                Check(!std::isnan(mean), "accumulated radiance is not NaN");
            }
        }
    }

    std::cout << (g_failures == 0 ? "ALL PASS" : "FAILURES")
              << " (" << g_failures << ")" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
