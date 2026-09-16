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

#ifndef LUX2_DYNLOAD_H
#define LUX2_DYNLOAD_H

#include <map>
#include <string>
#include <memory>
#include <vector>

#include "core/texture.h"
#include "core/material.h"
#include "core/light.h"
#include "core/shape.h"
#include "core/camera.h"
#include "core/sampler.h"
#include "core/filter.h"
#include "core/film.h"
#include "core/integrator.h"
#include "core/renderer.h"
#include "core/transform.h"

namespace lux2 {

class ParamSet;

// Everything a plugin factory needs to construct an instance.
struct PluginContext {
    // This plugin's own parameters.
    ParamSet *params = nullptr;

    // Object/texture/material-to-world transform for this plugin.
    Transform transform;

    // Scene-wide named texture tables.
    const std::map<std::string, std::shared_ptr<FloatTexture>>   *floatTextures   = nullptr;
    const std::map<std::string, std::shared_ptr<ColorTexture>>    *colorTextures   = nullptr;
    const std::map<std::string, std::shared_ptr<FresnelTexture>>  *fresnelTextures = nullptr;

    // Scene-wide named-material table.
    const std::map<std::string, std::shared_ptr<Material>> *namedMaterials = nullptr;

    // Downstream dependencies wired by the parser.
    Film   *film   = nullptr;
    Filter *filter = nullptr;
};

// Registry for all LuxRender plugin types.
class DynamicLoader {
public:

    template <class T>
    class RegisterLoader {
    public:
        RegisterLoader(std::map<std::string, T> &store,
                       const std::string &name,
                       T loader) {
            store[name] = loader;
        }
        virtual ~RegisterLoader() = default;
    };

    // Shape.
    typedef std::shared_ptr<Shape> (*CreateShape)(const PluginContext&);
    static std::map<std::string, CreateShape> &registeredShapes();
    template <class T>
    class RegisterShape : public RegisterLoader<CreateShape> {
    public:
        RegisterShape(const std::string &name)
            : RegisterLoader<CreateShape>(registeredShapes(), name, &T::CreateShape) {}
    };

    // Material.
    typedef std::shared_ptr<Material> (*CreateMaterial)(const PluginContext&);
    static std::map<std::string, CreateMaterial> &registeredMaterials();
    template <class T>
    class RegisterMaterial : public RegisterLoader<CreateMaterial> {
    public:
        RegisterMaterial(const std::string &name)
            : RegisterLoader<CreateMaterial>(registeredMaterials(), name, &T::CreateMaterial) {}
    };

    // Light.
    typedef std::shared_ptr<Light> (*CreateLight)(const PluginContext&);
    static std::map<std::string, CreateLight> &registeredLights();
    template <class T>
    class RegisterLight : public RegisterLoader<CreateLight> {
    public:
        RegisterLight(const std::string &name)
            : RegisterLoader<CreateLight>(registeredLights(), name, &T::CreateLight) {}
    };

    // FloatTexture.
    typedef std::shared_ptr<FloatTexture> (*CreateFloatTexture)(const PluginContext&);
    static std::map<std::string, CreateFloatTexture> &registeredFloatTextures();
    template <class T>
    class RegisterFloatTexture : public RegisterLoader<CreateFloatTexture> {
    public:
        RegisterFloatTexture(const std::string &name)
            : RegisterLoader<CreateFloatTexture>(registeredFloatTextures(), name, &T::CreateFloatTexture) {}
    };

    // ColorTexture.
    typedef std::shared_ptr<ColorTexture> (*CreateColorTexture)(const PluginContext&);
    static std::map<std::string, CreateColorTexture> &registeredColorTextures();
    template <class T>
    class RegisterColorTexture : public RegisterLoader<CreateColorTexture> {
    public:
        RegisterColorTexture(const std::string &name)
            : RegisterLoader<CreateColorTexture>(registeredColorTextures(), name, &T::CreateColorTexture) {}
    };

    // FresnelTexture.
    typedef std::shared_ptr<FresnelTexture> (*CreateFresnelTexture)(const PluginContext&);
    static std::map<std::string, CreateFresnelTexture> &registeredFresnelTextures();
    template <class T>
    class RegisterFresnelTexture : public RegisterLoader<CreateFresnelTexture> {
    public:
        RegisterFresnelTexture(const std::string &name)
            : RegisterLoader<CreateFresnelTexture>(registeredFresnelTextures(), name, &T::CreateFresnelTexture) {}
    };

    // Camera.
    typedef std::shared_ptr<Camera> (*CreateCamera)(const PluginContext&);
    static std::map<std::string, CreateCamera> &registeredCameras();
    template <class T>
    class RegisterCamera : public RegisterLoader<CreateCamera> {
    public:
        RegisterCamera(const std::string &name)
            : RegisterLoader<CreateCamera>(registeredCameras(), name, &T::CreateCamera) {}
    };

    // Sampler.
    typedef std::shared_ptr<Sampler> (*CreateSampler)(const PluginContext&);
    static std::map<std::string, CreateSampler> &registeredSamplers();
    template <class T>
    class RegisterSampler : public RegisterLoader<CreateSampler> {
    public:
        RegisterSampler(const std::string &name)
            : RegisterLoader<CreateSampler>(registeredSamplers(), name, &T::CreateSampler) {}
    };

    // Filter.
    typedef std::shared_ptr<Filter> (*CreateFilter)(const PluginContext&);
    static std::map<std::string, CreateFilter> &registeredFilters();
    template <class T>
    class RegisterFilter : public RegisterLoader<CreateFilter> {
    public:
        RegisterFilter(const std::string &name)
            : RegisterLoader<CreateFilter>(registeredFilters(), name, &T::CreateFilter) {}
    };

    // Film.
    typedef std::shared_ptr<Film> (*CreateFilm)(const PluginContext&);
    static std::map<std::string, CreateFilm> &registeredFilms();
    template <class T>
    class RegisterFilm : public RegisterLoader<CreateFilm> {
    public:
        RegisterFilm(const std::string &name)
            : RegisterLoader<CreateFilm>(registeredFilms(), name, &T::CreateFilm) {}
    };

    // SurfaceIntegrator.
    typedef std::shared_ptr<SurfaceIntegrator> (*CreateSurfaceIntegrator)(const PluginContext&);
    static std::map<std::string, CreateSurfaceIntegrator> &registeredSurfaceIntegrators();
    template <class T>
    class RegisterSurfaceIntegrator : public RegisterLoader<CreateSurfaceIntegrator> {
    public:
        RegisterSurfaceIntegrator(const std::string &name)
            : RegisterLoader<CreateSurfaceIntegrator>(registeredSurfaceIntegrators(), name, &T::CreateSurfaceIntegrator) {}
    };

    // VolumeIntegrator.
    typedef std::shared_ptr<VolumeIntegrator> (*CreateVolumeIntegrator)(const PluginContext&);
    static std::map<std::string, CreateVolumeIntegrator> &registeredVolumeIntegrators();
    template <class T>
    class RegisterVolumeIntegrator : public RegisterLoader<CreateVolumeIntegrator> {
    public:
        RegisterVolumeIntegrator(const std::string &name)
            : RegisterLoader<CreateVolumeIntegrator>(registeredVolumeIntegrators(), name, &T::CreateVolumeIntegrator) {}
    };

    // Renderer.
    typedef std::shared_ptr<Renderer> (*CreateRenderer)(const PluginContext&);
    static std::map<std::string, CreateRenderer> &registeredRenderers();
    template <class T>
    class RegisterRenderer : public RegisterLoader<CreateRenderer> {
    public:
        RegisterRenderer(const std::string &name)
            : RegisterLoader<CreateRenderer>(registeredRenderers(), name, &T::CreateRenderer) {}
    };

    // Human-readable list of every registered plugin, grouped by kind.
    static std::vector<std::string> GetRegisteredPlugins();
};

}  // namespace lux2

#endif // LUX2_DYNLOAD_H
