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

#ifndef LUX2_TONEMAP_H
#define LUX2_TONEMAP_H

#include "core/spectrum.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace lux2
{

    class ParamSet;

    // In place XYZ tonemap.
    class ToneMap
    {
    public:
        virtual ~ToneMap() = default;
        virtual void Map(std::vector<XYZColor> &xyz,
                         int xRes, int yRes, float maxDisplayY) const = 0;
    };

    // Registry.
    typedef std::unique_ptr<ToneMap> (*CreateToneMap)(const ParamSet &);
    std::map<std::string, CreateToneMap> &RegisteredToneMaps();
    std::unique_ptr<ToneMap> MakeToneMap(const std::string &name,
                                         const ParamSet &ps);

    template <class T>
    struct RegisterToneMapLoader
    {
        RegisterToneMapLoader(const std::string &name, CreateToneMap fn)
        {
            RegisteredToneMaps()[name] = fn;
        }
    };

} // namespace lux2

#define LUX2_REGISTER_TONEMAP(T, name)                                                \
    static ::lux2::RegisterToneMapLoader<T> __attribute__((used)) r_lux2_tonemap_##T( \
        name, &T::CreateToneMap)

#endif // LUX2_TONEMAP_H
