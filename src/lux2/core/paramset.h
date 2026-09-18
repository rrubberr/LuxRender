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

#ifndef LUX2_PARAMSET_H
#define LUX2_PARAMSET_H

#include "core/geometry.h"
#include "core/spectrum.h"
#include "core/error.h"

#include <string>
#include <vector>
#include <cstdint>

namespace lux2 {

// ParamType enum + LookupType declaration.
enum ParamType {
    PARAM_TYPE_INT, PARAM_TYPE_BOOL, PARAM_TYPE_FLOAT,
    PARAM_TYPE_POINT, PARAM_TYPE_VECTOR, PARAM_TYPE_NORMAL,
    PARAM_TYPE_COLOR, PARAM_TYPE_STRING, PARAM_TYPE_TEXTURE
};

// Parse a "<type> <name>" token.
bool LookupType(const char *token, ParamType *type, std::string &name);

// ParamSetItem<T> template.
template <class T>
struct ParamSetItem {
    ParamSetItem() : data(nullptr), nItems(0), lookedUp(false) { }
    ParamSetItem(const std::string &n, const T *v, std::uint32_t ni)
        : name(n), nItems(ni), lookedUp(false) {
        data = new T[nItems];
        for (std::uint32_t i = 0; i < nItems; ++i) data[i] = v[i];
    }
    ~ParamSetItem() { delete[] data; }

    ParamSetItem(const ParamSetItem &) = delete;
    ParamSetItem &operator=(const ParamSetItem &) = delete;

    // Copy into a fresh heap item.
    ParamSetItem *Clone() const {
        return new ParamSetItem<T>(name, data, nItems);
    }

    std::string name;
    std::uint32_t nItems;
    T *data;
    mutable bool lookedUp;
};

class ParamSet {
public:
    ParamSet() = default;
    ~ParamSet();

    // Deep copy (lux ParamSet idiom): clones every owned item so the copy
    // owns independent data. Enables by-value storage in SceneDescription.
    ParamSet(const ParamSet &p);
    ParamSet &operator=(const ParamSet &p);

    // Build a ParamSet from (token, value) pairs.
    ParamSet(std::uint32_t n, const char *pluginName,
             const char *const tokens[], const char *const params[]);

    // Add methods.
    void AddFloat(const std::string &name, const float *v, std::uint32_t n = 1);
    void AddInt(const std::string &name, const int *v, std::uint32_t n = 1);
    void AddBool(const std::string &name, const bool *v, std::uint32_t n = 1);
    void AddPoint(const std::string &name, const Point3f *v, std::uint32_t n = 1);
    void AddVector(const std::string &name, const Vector3f *v, std::uint32_t n = 1);
    void AddNormal(const std::string &name, const Normal3f *v, std::uint32_t n = 1);
    void AddRGBColor(const std::string &name, const RGBColor *v, std::uint32_t n = 1);
    void AddString(const std::string &name, const std::string *v, std::uint32_t n = 1);
    void AddTexture(const std::string &name, const std::string &value);

    // Erase methods).
    bool EraseInt(const std::string &name);
    bool EraseBool(const std::string &name);
    bool EraseFloat(const std::string &name);
    bool ErasePoint(const std::string &name);
    bool EraseVector(const std::string &name);
    bool EraseNormal(const std::string &name);
    bool EraseRGBColor(const std::string &name);
    bool EraseString(const std::string &name);
    bool EraseTexture(const std::string &name);

    // FindOne methods.
    float FindOneFloat(const std::string &name, float d) const;
    int FindOneInt(const std::string &name, int d) const;
    bool FindOneBool(const std::string &name, bool d) const;
    const Point3f &FindOnePoint(const std::string &name, const Point3f &d) const;
    const Vector3f &FindOneVector(const std::string &name, const Vector3f &d) const;
    const Normal3f &FindOneNormal(const std::string &name, const Normal3f &d) const;
    const RGBColor &FindOneRGBColor(const std::string &name, const RGBColor &d) const;
    const std::string &FindOneString(const std::string &name, const std::string &d) const;

    // Find (array) methods.
    const float *FindFloat(const std::string &name, std::uint32_t *nItems) const;
    const int *FindInt(const std::string &name, std::uint32_t *nItems) const;
    const bool *FindBool(const std::string &name, std::uint32_t *nItems) const;
    const Point3f *FindPoint(const std::string &name, std::uint32_t *nItems) const;
    const Vector3f *FindVector(const std::string &name, std::uint32_t *nItems) const;
    const Normal3f *FindNormal(const std::string &name, std::uint32_t *nItems) const;
    const RGBColor *FindRGBColor(const std::string &name, std::uint32_t *nItems) const;
    const std::string *FindString(const std::string &name, std::uint32_t *nItems) const;

    // Texture getter.
    std::string FindTexture(const std::string &name) const;

    // Housekeeping.
    void MarkAllUsed() const;
    void ReportUnused() const;
    void Clear();
    std::string ToString() const;

private:
    std::vector<ParamSetItem<int> *> ints;
    std::vector<ParamSetItem<bool> *> bools;
    std::vector<ParamSetItem<float> *> floats;
    std::vector<ParamSetItem<Point3f> *> points;
    std::vector<ParamSetItem<Vector3f> *> vectors;
    std::vector<ParamSetItem<Normal3f> *> normals;
    std::vector<ParamSetItem<RGBColor> *> spectra;
    std::vector<ParamSetItem<std::string> *> strings;
    std::vector<ParamSetItem<std::string> *> textures;
};

} // namespace lux2

#endif // LUX2_PARAMSET_H
