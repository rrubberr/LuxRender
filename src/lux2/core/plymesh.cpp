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

#include <luxrays/utils/ply/rply.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace lux2 {

using namespace luxrays;

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

// =======================================================================
// rply geometry read
// =======================================================================

namespace {

void PlyErrorCB(const char *message) {
    LOG(LUX_ERROR, LUX_SYSTEM) << "PLY loader error: " << message;
}

// Vertex position/normal/uv callback.
int PlyVertexCB(p_ply_argument argument) {
    long userIndex = 0;
    void *userData = nullptr;
    ply_get_argument_user_data(argument, &userData, &userIndex);
    float *c = *static_cast<float **>(userData);
    long vertIndex = 0;
    ply_get_argument_element(argument, nullptr, &vertIndex);
    const double v = ply_get_argument_value(argument);
    if (userIndex == 0)      c[3 * vertIndex + 0] = static_cast<float>(v);
    else if (userIndex == 1) c[3 * vertIndex + 1] = static_cast<float>(v);
    else if (userIndex == 2) c[3 * vertIndex + 2] = static_cast<float>(v);
    return 1;
}

int PlyTexCoordCB(p_ply_argument argument) {
    long userIndex = 0;
    void *userData = nullptr;
    ply_get_argument_user_data(argument, &userData, &userIndex);
    float *c = *static_cast<float **>(userData);
    long vertIndex = 0;
    ply_get_argument_element(argument, nullptr, &vertIndex);
    const double v = ply_get_argument_value(argument);
    if (userIndex == 0)      c[2 * vertIndex + 0] = static_cast<float>(v);
    else if (userIndex == 1) c[2 * vertIndex + 1] = static_cast<float>(v);
    return 1;
}

// Accumulates triangle and quad vertex indices across the face list.
struct PlyFaceData {
    std::vector<int> triVerts;
    std::vector<int> quadVerts;
};

int PlyFaceCB(p_ply_argument argument) {
    void *userData = nullptr;
    ply_get_argument_user_data(argument, &userData, nullptr);
    PlyFaceData *fd = static_cast<PlyFaceData *>(userData);

    long length = 0, valueIndex = 0;
    ply_get_argument_property(argument, nullptr, &length, &valueIndex);

    if (length == 3) {
        const long n = static_cast<long>(fd->triVerts.size());
        if (valueIndex < 0)
            fd->triVerts.resize(n + 3);        // preallocate pass
        else if (valueIndex < 3)
            fd->triVerts[n - 3 + valueIndex] =
                static_cast<int>(ply_get_argument_value(argument));
    } else if (length == 4) {
        const long n = static_cast<long>(fd->quadVerts.size());
        if (valueIndex < 0)
            fd->quadVerts.resize(n + 4);
        else if (valueIndex < 4)
            fd->quadVerts[n - 4 + valueIndex] =
                static_cast<int>(ply_get_argument_value(argument));
    }
    return 1;
}

} // anonymous namespace

bool ReadPlyGeometry(const std::string &path,
                     std::vector<Point3f> &P,
                     std::vector<Normal3f> &N,
                     std::vector<UV> &uv,
                     std::vector<std::array<int, 3>> &tris) {
    P.clear(); N.clear(); uv.clear(); tris.clear();

    p_ply plyfile = ply_open(path.c_str(), PlyErrorCB);
    if (!plyfile) {
        LOG(LUX_ERROR, LUX_SYSTEM) << "PLY: unable to open '" << path << "'";
        return false;
    }
    if (!ply_read_header(plyfile)) {
        LOG(LUX_ERROR, LUX_BADFILE) << "PLY: unable to read header '" << path << "'";
        ply_close(plyfile);
        return false;
    }

    float *pbuf = nullptr;
    const long nbVerts = ply_set_read_cb(plyfile, "vertex", "x",
                                         PlyVertexCB, &pbuf, 0);
    ply_set_read_cb(plyfile, "vertex", "y", PlyVertexCB, &pbuf, 1);
    ply_set_read_cb(plyfile, "vertex", "z", PlyVertexCB, &pbuf, 2);
    if (nbVerts <= 0) {
        LOG(LUX_ERROR, LUX_BADFILE) << "PLY: no vertices in '" << path << "'";
        ply_close(plyfile);
        return false;
    }

    PlyFaceData faceData;
    const long nbFaces = ply_set_read_cb(plyfile, "face", "vertex_indices",
                                         PlyFaceCB, &faceData, 0);
    if (nbFaces <= 0) {
        LOG(LUX_ERROR, LUX_BADFILE) << "PLY: no faces in '" << path << "'";
        ply_close(plyfile);
        return false;
    }

    float *nbuf = nullptr;
    const long nbNormals = ply_set_read_cb(plyfile, "vertex", "nx",
                                           PlyVertexCB, &nbuf, 0);
    ply_set_read_cb(plyfile, "vertex", "ny", PlyVertexCB, &nbuf, 1);
    ply_set_read_cb(plyfile, "vertex", "nz", PlyVertexCB, &nbuf, 2);

    // s/t preferred, then u/v.
    float *uvbuf = nullptr;
    long nbUVs = ply_set_read_cb(plyfile, "vertex", "s", PlyTexCoordCB, &uvbuf, 0);
    ply_set_read_cb(plyfile, "vertex", "t", PlyTexCoordCB, &uvbuf, 1);
    if (nbUVs <= 0) {
        nbUVs = ply_set_read_cb(plyfile, "vertex", "u", PlyTexCoordCB, &uvbuf, 0);
        ply_set_read_cb(plyfile, "vertex", "v", PlyTexCoordCB, &uvbuf, 1);
    }

    pbuf = new float[3 * nbVerts]();
    nbuf = (nbNormals > 0) ? new float[3 * nbVerts]() : nullptr;
    uvbuf = (nbUVs > 0) ? new float[2 * nbVerts]() : nullptr;

    const int readOk = ply_read(plyfile);
    ply_close(plyfile);
    if (!readOk) {
        LOG(LUX_ERROR, LUX_SYSTEM) << "PLY: parse failed '" << path << "'";
        delete[] pbuf; delete[] nbuf; delete[] uvbuf;
        return false;
    }

    const int nVerts = static_cast<int>(nbVerts);
    P.resize(nVerts);
    for (int i = 0; i < nVerts; ++i)
        P[i] = Point3f(pbuf[3 * i], pbuf[3 * i + 1], pbuf[3 * i + 2]);

    uv.assign(nVerts, UV(0.f, 0.f));
    if (uvbuf && nbUVs == nbVerts) {
        for (int i = 0; i < nVerts; ++i)
            uv[i] = UV(uvbuf[2 * i], uvbuf[2 * i + 1]);
    }

    // Quads split into (0,1,2) and (0,2,3).
    const int nbTris = static_cast<int>(faceData.triVerts.size()) / 3;
    const int nbQuads = static_cast<int>(faceData.quadVerts.size()) / 4;
    tris.reserve(nbTris + 2 * nbQuads);
    for (int f = 0; f < nbTris; ++f) {
        const int i = 3 * f;
        tris.push_back({faceData.triVerts[i], faceData.triVerts[i + 1],
                        faceData.triVerts[i + 2]});
    }
    for (int f = 0; f < nbQuads; ++f) {
        const int i = 4 * f;
        const int a = faceData.quadVerts[i + 0], b = faceData.quadVerts[i + 1],
                  c = faceData.quadVerts[i + 2], d = faceData.quadVerts[i + 3];
        tris.push_back({a, b, c});
        tris.push_back({a, c, d});
    }

    // Use file normals when present and complete, else generate
    // area-weighted face normals.
    N.assign(nVerts, Normal3f(0.f, 0.f, 0.f));
    if (nbuf && nbNormals == nbVerts) {
        for (int i = 0; i < nVerts; ++i)
            N[i] = Normal3f(nbuf[3 * i], nbuf[3 * i + 1], nbuf[3 * i + 2]);
    } else {
        std::vector<int> nf(nVerts, 0);
        auto addFaceNormal = [&](int i0, int i1, int i2) {
            const Vector3f e10 = P[i1] - P[i0];
            const Vector3f e12 = P[i1] - P[i2];
            const Vector3f fn = enoki::cross(e12, e10);
            N[i0] += fn; N[i1] += fn; N[i2] += fn;
            nf[i0]++; nf[i1]++; nf[i2]++;
        };
        for (const auto &t : tris)
            addFaceNormal(t[0], t[1], t[2]);
        for (int i = 0; i < nVerts; ++i)
            if (nf[i] > 0) N[i] /= static_cast<float>(nf[i]);
    }

    delete[] pbuf; delete[] nbuf; delete[] uvbuf;
    return true;
}

} // namespace lux2
