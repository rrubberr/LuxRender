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

#ifndef LUX2_ERROR_H
#define LUX2_ERROR_H

#include <iostream>
#include <sstream>
#include <string>

namespace lux2 {

enum LuxSeverity {
    LUX_SILENT = 0,
    LUX_ERROR = 1,
    LUX_WARNING = 2,
    LUX_INFO = 3
};

// Minimum severity that is actually printed. Defaults to LUX_WARNING.
inline int &LuxLogFilter() {
    static int filter = LUX_WARNING;
    return filter;
}

class LogStream {
public:
    LogStream(int severity) : m_severity(severity) { }
    ~LogStream() {
        if (m_severity <= LuxLogFilter()) {
            const char *tag =
                (m_severity == LUX_ERROR) ? "ERROR" :
                (m_severity == LUX_WARNING) ? "WARNING" : "INFO";
            std::cerr << "[lux2 " << tag << "] " << m_os.str() << std::endl;
        }
    }
    std::ostringstream &stream() { return m_os; }
private:
    int m_severity;
    std::ostringstream m_os;
};

// Usage: LOG(LUX_ERROR) << "bad value " << v;
// LogStream suppresses output itself when severity > LuxLogFilter().
#define LOG(severity) ::lux2::LogStream(severity).stream()

// Tag used by unimplemented C-API stubs (D4). Example:
//   LOG(LUX_ERROR) << LUX2_UNSUPPORTED_TAG << " luxSaveEXR";
#define LUX2_UNSUPPORTED_TAG "LUX2_UNSUPPORTED:"

} // namespace lux2

#endif // LUX2_ERROR_H
