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

#include "core/dynload.h"
#include <string>
#include <vector>

namespace lux2 {

std::map<std::string, DynamicLoader::CreateShape> &DynamicLoader::registeredShapes() {
    static std::map<std::string, DynamicLoader::CreateShape> *m =
        new std::map<std::string, DynamicLoader::CreateShape>;
    return *m;
}

std::map<std::string, DynamicLoader::CreateMaterial> &DynamicLoader::registeredMaterials() {
    static std::map<std::string, DynamicLoader::CreateMaterial> *m =
        new std::map<std::string, DynamicLoader::CreateMaterial>;
    return *m;
}

std::map<std::string, DynamicLoader::CreateLight> &DynamicLoader::registeredLights() {
    static std::map<std::string, DynamicLoader::CreateLight> *m =
        new std::map<std::string, DynamicLoader::CreateLight>;
    return *m;
}

std::map<std::string, DynamicLoader::CreateFloatTexture> &DynamicLoader::registeredFloatTextures() {
    static std::map<std::string, DynamicLoader::CreateFloatTexture> *m =
        new std::map<std::string, DynamicLoader::CreateFloatTexture>;
    return *m;
}

std::map<std::string, DynamicLoader::CreateColorTexture> &DynamicLoader::registeredColorTextures() {
    static std::map<std::string, DynamicLoader::CreateColorTexture> *m =
        new std::map<std::string, DynamicLoader::CreateColorTexture>;
    return *m;
}

std::map<std::string, DynamicLoader::CreateFresnelTexture> &DynamicLoader::registeredFresnelTextures() {
    static std::map<std::string, DynamicLoader::CreateFresnelTexture> *m =
        new std::map<std::string, DynamicLoader::CreateFresnelTexture>;
    return *m;
}

std::map<std::string, DynamicLoader::CreateCamera> &DynamicLoader::registeredCameras() {
    static std::map<std::string, DynamicLoader::CreateCamera> *m =
        new std::map<std::string, DynamicLoader::CreateCamera>;
    return *m;
}

std::map<std::string, DynamicLoader::CreateSampler> &DynamicLoader::registeredSamplers() {
    static std::map<std::string, DynamicLoader::CreateSampler> *m =
        new std::map<std::string, DynamicLoader::CreateSampler>;
    return *m;
}

std::map<std::string, DynamicLoader::CreateFilter> &DynamicLoader::registeredFilters() {
    static std::map<std::string, DynamicLoader::CreateFilter> *m =
        new std::map<std::string, DynamicLoader::CreateFilter>;
    return *m;
}

std::map<std::string, DynamicLoader::CreateFilm> &DynamicLoader::registeredFilms() {
    static std::map<std::string, DynamicLoader::CreateFilm> *m =
        new std::map<std::string, DynamicLoader::CreateFilm>;
    return *m;
}

std::map<std::string, DynamicLoader::CreateSurfaceIntegrator> &DynamicLoader::registeredSurfaceIntegrators() {
    static std::map<std::string, DynamicLoader::CreateSurfaceIntegrator> *m =
        new std::map<std::string, DynamicLoader::CreateSurfaceIntegrator>;
    return *m;
}

std::map<std::string, DynamicLoader::CreateVolumeIntegrator> &DynamicLoader::registeredVolumeIntegrators() {
    static std::map<std::string, DynamicLoader::CreateVolumeIntegrator> *m =
        new std::map<std::string, DynamicLoader::CreateVolumeIntegrator>;
    return *m;
}

std::map<std::string, DynamicLoader::CreateRenderer> &DynamicLoader::registeredRenderers() {
    static std::map<std::string, DynamicLoader::CreateRenderer> *m =
        new std::map<std::string, DynamicLoader::CreateRenderer>;
    return *m;
}

std::vector<std::string> DynamicLoader::GetRegisteredPlugins() {
    std::vector<std::string> out;
    auto add = [&out](const char *kind,
                      const std::vector<std::string> &names) {
        for (const std::string &n : names)
            out.push_back(std::string(kind) + ":" + n);
    };
    auto keys = [](auto &m) {
        std::vector<std::string> v;
        for (const auto &kv : m) v.push_back(kv.first);
        return v;
    };
    add("Shape", keys(registeredShapes()));
    add("Material", keys(registeredMaterials()));
    add("Light", keys(registeredLights()));
    add("FloatTexture", keys(registeredFloatTextures()));
    add("ColorTexture", keys(registeredColorTextures()));
    add("FresnelTexture", keys(registeredFresnelTextures()));
    add("Camera", keys(registeredCameras()));
    add("Sampler", keys(registeredSamplers()));
    add("Filter", keys(registeredFilters()));
    add("Film", keys(registeredFilms()));
    add("SurfaceIntegrator", keys(registeredSurfaceIntegrators()));
    add("VolumeIntegrator", keys(registeredVolumeIntegrators()));
    add("Renderer", keys(registeredRenderers()));
    return out;
}

}  // namespace lux2
