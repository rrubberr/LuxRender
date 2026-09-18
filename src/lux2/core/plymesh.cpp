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

#include "core/plymesh.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

namespace lux2 {
namespace {

// Byte width of a ply scalar type token (e.g. "float" -> 4). Returns 0 for
// unknown tokens.
long ScalarSize(const std::string &t) {
    if (t == "char" || t == "int8") return 1;
    if (t == "uchar" || t == "uint8") return 1;
    if (t == "short" || t == "int16") return 2;
    if (t == "ushort" || t == "uint16") return 2;
    if (t == "int" || t == "int32" || t == "uint" || t == "uint32") return 4;
    if (t == "float" || t == "float32") return 4;
    if (t == "double" || t == "float64") return 8;
    return 0;
}

bool ReadLine(std::FILE *f, std::string &out) {
    int c = std::fgetc(f);
    if (c == EOF)
        return false;
    out.clear();
    while (c != EOF && c != '\n') {
        out.push_back(static_cast<char>(c));
        c = std::fgetc(f);
    }
    return true;
}

std::string Trim(const std::string &s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::vector<std::string> SplitWs(const std::string &s) {
    std::vector<std::string> parts;
    std::string cur;
    for (char c : s) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!cur.empty()) { parts.push_back(cur); cur.clear(); }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) parts.push_back(cur);
    return parts;
}

} // anonymous namespace

PlySummary ReadPlySummary(const std::string &path) {
    PlySummary out;

    std::FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) {
        LOG(LUX_ERROR, LUX_NOFILE) << "PLY: could not open '" << path << "'";
        return out;
    }

    std::string line = Trim([f]{ std::string s; ReadLine(f, s); return s; }());
    if (line != "ply") {
        LOG(LUX_ERROR, LUX_BADFILE) << "PLY: missing magic in '" << path << "'";
        std::fclose(f);
        return out;
    }

    bool ascii = false, binaryLE = false, binaryBE = false;
    long vertexCount = 0, faceCount = 0;
    long vertexStride = 0, posOffset = -1;
    bool inVertex = false;

    while (ReadLine(f, line)) {
        line = Trim(line);
        if (line == "end_header")
            break;

        std::vector<std::string> w = SplitWs(line);
        if (w.empty())
            continue;

        if (w[0] == "format") {
            if (w.size() >= 2 && w[1] == "ascii") ascii = true;
            else if (w.size() >= 2 && w[1] == "binary_little_endian") binaryLE = true;
            else if (w.size() >= 2 && w[1] == "binary_big_endian") binaryBE = true;
        } else if (w[0] == "element") {
            inVertex = (w.size() >= 2 && w[1] == "vertex");
            if (w.size() >= 3) {
                long n = std::atol(w[2].c_str());
                if (inVertex) vertexCount = n;
                else if (w[1] == "face") faceCount = n;
            }
            if (inVertex) { vertexStride = 0; posOffset = -1; }
        } else if (w[0] == "property" && inVertex) {
            if (w.size() >= 2 && w[1] == "list") {
                LOG(LUX_ERROR, LUX_BADFILE)
                    << "PLY: list property on vertex in '" << path << "'";
                std::fclose(f);
                return out;
            }
            if (w.size() >= 3) {
                const long size = ScalarSize(w[1]);
                if (size == 0) {
                    LOG(LUX_ERROR, LUX_BADFILE)
                        << "PLY: unsupported property type '" << w[1]
                        << "' in '" << path << "'";
                    std::fclose(f);
                    return out;
                }
                if (w[2] == "x" && posOffset < 0)
                    posOffset = vertexStride;
                vertexStride += size;
            }
        }
    }

    out.faceCount = faceCount > 0 ? static_cast<std::uint64_t>(faceCount) : 0;

    // Binary files: read vertex positions to compute the object-space bound.
    if (!ascii && (binaryLE || binaryBE) && vertexCount > 0 &&
        posOffset >= 0 && vertexStride > 0) {
        std::vector<char> rec(static_cast<std::size_t>(vertexStride));
        BBox bound;
        bool good = true;
        for (long i = 0; i < vertexCount; ++i) {
            if (std::fread(rec.data(), 1,
                    static_cast<std::size_t>(vertexStride), f) !=
                    static_cast<std::size_t>(vertexStride)) {
                good = false;
                break;
            }
            float xyz[3];
            for (int k = 0; k < 3; ++k) {
                float v;
                std::memcpy(&v, rec.data() + posOffset + 4 * k, sizeof(v));
                if (binaryBE) {
                    unsigned char *b = reinterpret_cast<unsigned char *>(&v);
                    std::reverse(b, b + 4);
                }
                xyz[k] = v;
            }
            bound = Union(bound, Point3f(xyz[0], xyz[1], xyz[2]));
        }
        if (good) {
            out.bound = bound;
        } else {
            LOG(LUX_WARNING, LUX_BADFILE)
                << "PLY: short read computing bound for '" << path << "'";
        }
    }

    std::fclose(f);
    out.ok = true;
    return out;
}

} // namespace lux2
