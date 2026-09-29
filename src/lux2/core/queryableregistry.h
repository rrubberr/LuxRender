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

#ifndef LUX2_QUERYABLEREGISTRY_H
#define LUX2_QUERYABLEREGISTRY_H

#include "core/queryable.h"

#include <map>
#include <mutex>
#include <string>

namespace lux2
{

    // Collection of named Queryable objects.
    class QueryableRegistry
    {
    public:
        void Insert(Queryable *object);
        void Erase(const std::string &name);
        void Clear();

        // Returns the object or nullptr.
        Queryable *operator[](const std::string &name) const;

        bool Has(const std::string &name) const;

        // XML dump of every registered object and attribute.
        const char *GetContent();

    private:
        mutable std::mutex m_mutex;
        std::map<std::string, Queryable *> m_objects;
        std::string m_xmlString;
    };

} // namespace lux2

#endif // LUX2_QUERYABLEREGISTRY_H
