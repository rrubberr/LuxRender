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

#include "core/tonemap.h"

namespace lux2
{

    std::map<std::string, CreateToneMap> &RegisteredToneMaps()
    {
        static std::map<std::string, CreateToneMap> registry;
        return registry;
    }

    std::unique_ptr<ToneMap> MakeToneMap(const std::string &name,
                                         const ParamSet &ps)
    {
        auto &registry = RegisteredToneMaps();
        auto i = registry.find(name);
        if (i == registry.end())
            return nullptr;
        return std::unique_ptr<ToneMap>(i->second(ps));
    }

} // namespace lux2
