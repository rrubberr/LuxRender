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

#ifndef LUX2_GRAPHICSSTATE_H
#define LUX2_GRAPHICSSTATE_H

// Parser graphics state.

#include "core/paramset.h"

#include <map>
#include <string>

namespace lux2 {

// A named texture binding recorded by the Texture statement.
struct TextureDesc {
    std::string textureType;   // "float" | "color" | "fresnel"
    std::string pluginName;    // e.g. "fresnelcolor", "constant"
    ParamSet params;
};

// A material binding on a shape.
struct MaterialBinding {
    bool isNamed = false;      // True when namedRef is a namedMaterial.
    std::string namedRef;      // When isNamed.
    std::string pluginName;    // When inline.
    ParamSet params;           // When inline.

    bool valid() const { return isNamed || !pluginName.empty(); }
};

// The scoped graphics state.
struct GraphicsState {
    // Named texture tables.
    std::map<std::string, TextureDesc> floatTextures;
    std::map<std::string, TextureDesc> colorTextures;
    std::map<std::string, TextureDesc> fresnelTextures;

    // Named materials visible in this scope.
    std::map<std::string, ParamSet> namedMaterials;

    // Current material binding applied to subsequently declared shapes.
    MaterialBinding material;

    // Pending area light: set by AreaLightSource.
    bool areaLightActive = false;
    std::string areaLightName;
    ParamSet areaLightParams;

    // Current light group name.
    std::string currentLightGroup;

    bool reverseOrientation = false;
};

} // namespace lux2

#endif // LUX2_GRAPHICSSTATE_H
