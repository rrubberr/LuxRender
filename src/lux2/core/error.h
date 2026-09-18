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

// Symbol-visibility marker for the C API.
#ifndef LUX_EXPORT
#define LUX_EXPORT
#endif

namespace lux2 {

// Log severity levels, ordered from least to most verbose.
enum LuxSeverity {
    LUX_SILENT = 0,
    LUX_FATAL = 1,
    LUX_SEVERE = 2,
    LUX_ERROR = 3,
    LUX_WARNING = 4,
    LUX_INFO = 5
};

// Minimum severity that is actually printed. Defaults to LUX_WARNING.
inline int &LuxLogFilter() {
    static int filter = LUX_WARNING;
    return filter;
}

// The most recent error code reported through LOG.
inline int &LuxLastError() {
    static int lastError = 0;
    return lastError;
}

class LogStream {
public:
    LogStream(int severity, int code) : m_severity(severity), m_code(code) { }
    ~LogStream() {
        if (m_code != 0)
            LuxLastError() = m_code;
        if (m_severity <= LuxLogFilter()) {
            const char *tag =
                (m_severity <= LUX_SEVERE) ? "SEVERE" :
                (m_severity == LUX_ERROR) ? "ERROR" :
                (m_severity == LUX_WARNING) ? "WARNING" : "INFO";
            std::cerr << "[lux2 " << tag << "] " << m_os.str() << std::endl;
        }
    }
    std::ostringstream &stream() { return m_os; }

private:
    int m_severity;
    int m_code;
    std::ostringstream m_os;
};

} // namespace lux2

// Error codes.
#define LUX_NOERROR         0

#define LUX_NOMEM           1       // Out of memory
#define LUX_SYSTEM          2       // Miscellaneous system error
#define LUX_NOFILE          3       // File nonexistent
#define LUX_BADFILE         4       // Bad file format
#define LUX_BADVERSION      5       // File version mismatch
#define LUX_DISKFULL        6       // Target disk is full

#define LUX_UNIMPLEMENT    12       // Unimplemented feature
#define LUX_LIMIT          13       // Arbitrary program limit
#define LUX_BUG            14       // Probably a bug in renderer

#define LUX_NOTSTARTED     23       // luxInit() not called
#define LUX_NESTING        24       // Bad begin-end nesting
#define LUX_NOTOPTIONS     25       // Invalid state for options
#define LUX_NOTATTRIBS     26       // Invalid state for attributes
#define LUX_NOTPRIMS       27       // Invalid state for primitives
#define LUX_ILLSTATE       28       // Other invalid state
#define LUX_BADMOTION      29       // Badly formed motion block
#define LUX_BADSOLID       30       // Badly formed solid block

#define LUX_BADTOKEN       41       // Invalid token for request
#define LUX_RANGE          42       // Parameter out of range
#define LUX_CONSISTENCY    43       // Parameters inconsistent
#define LUX_BADHANDLE      44       // Bad object/light handle
#define LUX_NOPLUGIN       45       // Can't load requested plugin
#define LUX_MISSINGDATA    46       // Required parameters not provided
#define LUX_SYNTAX         47       // Declare type syntax error

#define LUX_MATH           61       // Zerodivide, noninvert matrix, etc.

// Usage: LOG(LUX_ERROR, LUX_ILLSTATE) << "bad value " << v;
// The LogStream suppresses output itself when severity > LuxLogFilter().
#define LOG(severity, code) ::lux2::LogStream(severity, code).stream()

// Tag used by unimplemented C-API stubs (D4). Example:
//   LOG(LUX_ERROR, LUX_UNIMPLEMENT) << LUX2_UNSUPPORTED_TAG << " luxSaveEXR";
#define LUX2_UNSUPPORTED_TAG "LUX2_UNSUPPORTED:"

#endif // LUX2_ERROR_H
