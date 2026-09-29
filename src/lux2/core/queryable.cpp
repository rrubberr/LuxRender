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

#include "core/queryable.h"

#include <cstdio>

namespace lux2
{

    std::string QueryableAttribute::TypeStr() const
    {
        switch (Type())
        {
        case AttributeType::None:
            return "none";
        case AttributeType::Bool:
            return "bool";
        case AttributeType::Int:
            return "int";
        case AttributeType::Float:
            return "float";
        case AttributeType::Double:
            return "double";
        case AttributeType::String:
            return "string";
        }
        return "invalid";
    }

    namespace
    {
        // Shortest reasonable decimal form, trimming trailing zeros.
        std::string FormatReal(double v)
        {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%g", v);
            return buf;
        }
    } // namespace

    std::string QueryableFloatAttribute::Value() const { return FormatReal(getFunc()); }
    std::string QueryableFloatAttribute::DefaultValue() const
    {
        return hasDefaultValue ? FormatReal(defaultValue) : std::string();
    }

    std::string QueryableDoubleAttribute::Value() const { return FormatReal(getFunc()); }
    std::string QueryableDoubleAttribute::DefaultValue() const
    {
        return hasDefaultValue ? FormatReal(defaultValue) : std::string();
    }

    std::string FormatTemplate(const Queryable &obj, const std::string &tpl)
    {
        std::string out;
        out.reserve(tpl.size());
        for (size_t i = 0; i < tpl.size();)
        {
            if (tpl[i] != '%')
            {
                out += tpl[i++];
                continue;
            }
            // '%' seen.
            if (i + 1 < tpl.size() && tpl[i + 1] == '%')
            {
                out += '%'; // escaped literal
                i += 2;
                continue;
            }
            size_t close = tpl.find('%', i + 1);
            if (close == std::string::npos)
            {
                out += tpl.substr(i); // trailing lone '%'
                break;
            }
            const std::string token = tpl.substr(i + 1, close - i - 1);
            if (obj.HasAttribute(token))
                out += obj[token].Value();
            i = close + 1;
        }
        return out;
    }

} // namespace lux2
