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

#include <map>
#include <string>
#include <memory>
#include <vector>

namespace lux2
{

    class ParamSet;

    // Everything a plugin factory needs to construct an instance.
    struct PluginContext
    {
        // This plugin's own parameters.
        ParamSet *params = nullptr;

        // Object/texture/material-to-world transform for this plugin.
        Transform transform;

        // Named texture tables.
        const std::map<std::string, std::shared_ptr<FloatTexture>> *floatTextures = nullptr;
        const std::map<std::string, std::shared_ptr<ColorTexture>> *colorTextures = nullptr;
        const std::map<std::string, std::shared_ptr<FresnelTexture>> *fresnelTextures = nullptr;

        // Named material table.
        const std::map<std::string, std::shared_ptr<Material>> *namedMaterials = nullptr;

        // Downstream dependencies wired by the parser.
        Film *film = nullptr;
        Filter *filter = nullptr;

        // The Film's parameters.
        const ParamSet *filmParams = nullptr;
    };

    // Registry for all plugin types.
    class DynamicLoader
    {
    public:
        template <class T>
        class RegisterLoader
        {
        public:
            RegisterLoader(std::map<std::string, T> &store,
                           const std::string &name,
                           T loader)
            {
                store[name] = loader;
            }
            virtual ~RegisterLoader() = default;
        };

        // Each plugin kind needs: a Create function-pointer typedef, a registry
        // accessor, and a Register<Kind> helper.
#define LUX2_REGISTER_PLUGIN(Kind)                                        \
    typedef std::shared_ptr<Kind> (*Create##Kind)(const PluginContext &); \
    static std::map<std::string, Create##Kind> &registered##Kind##s();    \
    template <class T>                                                    \
    class Register##Kind : public RegisterLoader<Create##Kind>            \
    {                                                                     \
    public:                                                               \
        Register##Kind(const std::string &name)                           \
            : RegisterLoader<Create##Kind>(registered##Kind##s(), name,   \
                                           &T::Create##Kind) {}           \
    };

        LUX2_REGISTER_PLUGIN(Shape)
        LUX2_REGISTER_PLUGIN(Material)
        LUX2_REGISTER_PLUGIN(Light)
        LUX2_REGISTER_PLUGIN(FloatTexture)
        LUX2_REGISTER_PLUGIN(ColorTexture)
        LUX2_REGISTER_PLUGIN(FresnelTexture)
        LUX2_REGISTER_PLUGIN(Camera)
        LUX2_REGISTER_PLUGIN(Sampler)
        LUX2_REGISTER_PLUGIN(Filter)
        LUX2_REGISTER_PLUGIN(Film)
        LUX2_REGISTER_PLUGIN(SurfaceIntegrator)
        LUX2_REGISTER_PLUGIN(VolumeIntegrator)
        LUX2_REGISTER_PLUGIN(Renderer)

#undef LUX2_REGISTER_PLUGIN

        // Human readable list of every registered plugin.
        static std::vector<std::string> GetRegisteredPlugins();
    };

} // namespace lux2

#endif // LUX2_DYNLOAD_H
