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

#include "core/queryableregistry.h"

#include <sstream>

namespace lux2
{

    void QueryableRegistry::Insert(Queryable *object)
    {
        if (!object)
            return;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_objects[object->GetName()] = object;
    }

    void QueryableRegistry::Erase(const std::string &name)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_objects.erase(name);
    }

    void QueryableRegistry::Clear()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_objects.clear();
    }

    Queryable *QueryableRegistry::operator[](const std::string &name) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_objects.find(name);
        return it != m_objects.end() ? it->second : nullptr;
    }

    bool QueryableRegistry::Has(const std::string &name) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_objects.find(name) != m_objects.end();
    }

    const char *QueryableRegistry::GetContent()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::ostringstream xml;
        xml << "<?xml version='1.0' encoding='utf-8'?>\n<context>\n";
        for (const auto &obj : m_objects)
        {
            xml << "  <object>\n";
            xml << "    <name>" << obj.first << "</name>\n";
            for (auto it = obj.second->begin(); it != obj.second->end(); ++it)
            {
                const QueryableAttribute &attr = *it->second;
                xml << "    <attribute>\n";
                xml << "      <name>" << attr.Name() << "</name>\n";
                xml << "      <type>" << attr.TypeStr() << "</type>\n";
                xml << "      <description>" << attr.Description() << "</description>\n";
                xml << "      <value>" << attr.Value() << "</value>\n";
                if (attr.HasDefaultValue())
                    xml << "      <default>" << attr.DefaultValue() << "</default>\n";
                xml << "    </attribute>\n";
            }
            xml << "  </object>\n";
        }
        xml << "</context>\n";
        m_xmlString = xml.str();
        return m_xmlString.c_str();
    }

} // namespace lux2
