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

#ifndef LUX2_REGISTER_H
#define LUX2_REGISTER_H

#include "core/dynload.h"

// Self registration macros.

#define LUX2_REG_CONCAT_INNER(a, b) a##b
#define LUX2_REG_CONCAT(a, b) LUX2_REG_CONCAT_INNER(a, b)

#define LUX2_REGISTER_PLUGIN(RegClass, T, name)                     \
    static ::lux2::DynamicLoader::RegClass<T> __attribute__((used)) \
    LUX2_REG_CONCAT(lux2Reg, __LINE__)(name)

#define LUX2_REGISTER_SHAPE(T, name) LUX2_REGISTER_PLUGIN(RegisterShape, T, name)
#define LUX2_REGISTER_MATERIAL(T, name) LUX2_REGISTER_PLUGIN(RegisterMaterial, T, name)
#define LUX2_REGISTER_LIGHT(T, name) LUX2_REGISTER_PLUGIN(RegisterLight, T, name)
#define LUX2_REGISTER_FLOAT_TEXTURE(T, name) LUX2_REGISTER_PLUGIN(RegisterFloatTexture, T, name)
#define LUX2_REGISTER_COLOR_TEXTURE(T, name) LUX2_REGISTER_PLUGIN(RegisterColorTexture, T, name)
#define LUX2_REGISTER_FRESNEL_TEXTURE(T, name) LUX2_REGISTER_PLUGIN(RegisterFresnelTexture, T, name)
#define LUX2_REGISTER_CAMERA(T, name) LUX2_REGISTER_PLUGIN(RegisterCamera, T, name)
#define LUX2_REGISTER_SAMPLER(T, name) LUX2_REGISTER_PLUGIN(RegisterSampler, T, name)
#define LUX2_REGISTER_FILTER(T, name) LUX2_REGISTER_PLUGIN(RegisterFilter, T, name)
#define LUX2_REGISTER_FILM(T, name) LUX2_REGISTER_PLUGIN(RegisterFilm, T, name)
#define LUX2_REGISTER_SURFACE_INTEGRATOR(T, name) LUX2_REGISTER_PLUGIN(RegisterSurfaceIntegrator, T, name)
#define LUX2_REGISTER_VOLUME_INTEGRATOR(T, name) LUX2_REGISTER_PLUGIN(RegisterVolumeIntegrator, T, name)
#define LUX2_REGISTER_RENDERER(T, name) LUX2_REGISTER_PLUGIN(RegisterRenderer, T, name)

#endif // LUX2_REGISTER_H
