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

#ifndef LUX2_TRANSFORM_STACK_H
#define LUX2_TRANSFORM_STACK_H

// Current object-to-world transform.

#include "core/transform.h"

#include <map>
#include <string>
#include <vector>

namespace lux2 {

class TransformStack {
public:
    TransformStack() = default;

    // The current object-to-world transform.
    const Transform &current() const { return m_current; }

    // Post-multiply the current transform (Translate/Rotate/Scale/Concat/LookAt).
    void postMultiply(const Transform &t) { m_current = m_current * t; }

    // Replace the current transform.
    void set(const Transform &t) { m_current = t; }

    void identity() { m_current = Transform(); }

    // TransformBegin/End.
    void push() { m_stack.push_back(m_current); }
    void pop() {
        if (!m_stack.empty()) {
            m_current = m_stack.back();
            m_stack.pop_back();
        }
    }

    // CoordinateSystem / CoordSysTransform.
    void nameCoordinateSystem(const std::string &name) {
        m_named[name] = m_current;
    }
    bool useCoordinateSystem(const std::string &name) {
        auto it = m_named.find(name);
        if (it == m_named.end())
            return false;
        m_current = it->second;
        return true;
    }

private:
    Transform m_current;                 // Identity by default!
    std::vector<Transform> m_stack;
    std::map<std::string, Transform> m_named;
};

} // namespace lux2

#endif // LUX2_TRANSFORM_STACK_H
