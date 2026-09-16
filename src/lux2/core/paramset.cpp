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

#include "core/paramset.h"

#include <cctype>
#include <cstring>
#include <sstream>

namespace lux2 {

// ---------------------------------------------------------------------------
// LookupType
// ---------------------------------------------------------------------------
bool LookupType(const char *token, ParamType *type, std::string &name) {
    *type = PARAM_TYPE_INT;  // dummy init
    const char *strp = token;
    while (*strp && std::isspace(static_cast<unsigned char>(*strp)))
        ++strp;
    if (!*strp) {
        LOG(LUX_ERROR) << "Parameter '" << token << "' doesn't have a type declaration?!";
        name = token;
        return false;
    }

#define TRY_DECODING_TYPE(keyword, mask) \
    if (strncmp(keyword, strp, strlen(keyword)) == 0) { \
        *type = mask; strp += strlen(keyword); \
    }
    TRY_DECODING_TYPE("float", PARAM_TYPE_FLOAT)
    else TRY_DECODING_TYPE("integer", PARAM_TYPE_INT)
    else TRY_DECODING_TYPE("bool", PARAM_TYPE_BOOL)
    else TRY_DECODING_TYPE("point", PARAM_TYPE_POINT)
    else TRY_DECODING_TYPE("vector", PARAM_TYPE_VECTOR)
    else TRY_DECODING_TYPE("normal", PARAM_TYPE_NORMAL)
    else TRY_DECODING_TYPE("string", PARAM_TYPE_STRING)
    else TRY_DECODING_TYPE("texture", PARAM_TYPE_TEXTURE)
    else TRY_DECODING_TYPE("color", PARAM_TYPE_COLOR)
    else {
        LOG(LUX_ERROR) << "Unable to decode type for token '" << token << "'";
        name = token;
        return false;
    }
#undef TRY_DECODING_TYPE

    while (*strp && std::isspace(static_cast<unsigned char>(*strp)))
        ++strp;
    name = strp;
    return true;
}

// ---------------------------------------------------------------------------
// Helpers to avoid duplication
// ---------------------------------------------------------------------------
template <class T>
static bool EraseItem(std::vector<ParamSetItem<T> *> &v, const std::string &name) {
    for (size_t i = 0; i < v.size(); ++i)
        if (v[i]->name == name) {
            delete v[i];
            v.erase(v.begin() + i);
            return true;
        }
    return false;
}

template <class T>
static ParamSetItem<T> *FindItem(std::vector<ParamSetItem<T> *> &v, const std::string &name) {
    for (size_t i = 0; i < v.size(); ++i)
        if (v[i]->name == name)
            return v[i];
    return nullptr;
}

template <class T>
static const ParamSetItem<T> *FindItemConst(const std::vector<ParamSetItem<T> *> &v,
                                             const std::string &name) {
    for (size_t i = 0; i < v.size(); ++i)
        if (v[i]->name == name)
            return v[i];
    return nullptr;
}

// ---------------------------------------------------------------------------
// ParamSet methods
// ---------------------------------------------------------------------------

// Add methods.
void ParamSet::AddFloat(const std::string &name, const float *v, std::uint32_t n) {
    floats.push_back(new ParamSetItem<float>(name, v, n));
}
void ParamSet::AddInt(const std::string &name, const int *v, std::uint32_t n) {
    ints.push_back(new ParamSetItem<int>(name, v, n));
}
void ParamSet::AddBool(const std::string &name, const bool *v, std::uint32_t n) {
    bools.push_back(new ParamSetItem<bool>(name, v, n));
}
void ParamSet::AddPoint(const std::string &name, const Point3f *v, std::uint32_t n) {
    points.push_back(new ParamSetItem<Point3f>(name, v, n));
}
void ParamSet::AddVector(const std::string &name, const Vector3f *v, std::uint32_t n) {
    vectors.push_back(new ParamSetItem<Vector3f>(name, v, n));
}
void ParamSet::AddNormal(const std::string &name, const Normal3f *v, std::uint32_t n) {
    normals.push_back(new ParamSetItem<Normal3f>(name, v, n));
}
void ParamSet::AddRGBColor(const std::string &name, const RGBColor *v, std::uint32_t n) {
    spectra.push_back(new ParamSetItem<RGBColor>(name, v, n));
}
void ParamSet::AddString(const std::string &name, const std::string *v, std::uint32_t n) {
    strings.push_back(new ParamSetItem<std::string>(name, v, n));
}
void ParamSet::AddTexture(const std::string &name, const std::string &value) {
    textures.push_back(new ParamSetItem<std::string>(name, &value, 1));
}

// Erase methods.
bool ParamSet::EraseInt(const std::string &name) { return EraseItem(ints, name); }
bool ParamSet::EraseBool(const std::string &name) { return EraseItem(bools, name); }
bool ParamSet::EraseFloat(const std::string &name) { return EraseItem(floats, name); }
bool ParamSet::ErasePoint(const std::string &name) { return EraseItem(points, name); }
bool ParamSet::EraseVector(const std::string &name) { return EraseItem(vectors, name); }
bool ParamSet::EraseNormal(const std::string &name) { return EraseItem(normals, name); }
bool ParamSet::EraseRGBColor(const std::string &name) { return EraseItem(spectra, name); }
bool ParamSet::EraseString(const std::string &name) { return EraseItem(strings, name); }
bool ParamSet::EraseTexture(const std::string &name) { return EraseItem(textures, name); }

// FindOne methods (scalar return).
float ParamSet::FindOneFloat(const std::string &name, float d) const {
    const auto *item = FindItemConst(floats, name);
    if (item && item->nItems >= 1) {
        const_cast<ParamSetItem<float> *>(item)->lookedUp = true;
        return item->data[0];
    }
    return d;
}
int ParamSet::FindOneInt(const std::string &name, int d) const {
    const auto *item = FindItemConst(ints, name);
    if (item && item->nItems >= 1) {
        const_cast<ParamSetItem<int> *>(item)->lookedUp = true;
        return item->data[0];
    }
    return d;
}
bool ParamSet::FindOneBool(const std::string &name, bool d) const {
    const auto *item = FindItemConst(bools, name);
    if (item && item->nItems >= 1) {
        const_cast<ParamSetItem<bool> *>(item)->lookedUp = true;
        return item->data[0];
    }
    return d;
}

// FindOne methods (reference return).
const Point3f &ParamSet::FindOnePoint(const std::string &name, const Point3f &d) const {
    const auto *item = FindItemConst(points, name);
    if (item && item->nItems >= 1) {
        const_cast<ParamSetItem<Point3f> *>(item)->lookedUp = true;
        return item->data[0];
    }
    return d;
}
const Vector3f &ParamSet::FindOneVector(const std::string &name, const Vector3f &d) const {
    const auto *item = FindItemConst(vectors, name);
    if (item && item->nItems >= 1) {
        const_cast<ParamSetItem<Vector3f> *>(item)->lookedUp = true;
        return item->data[0];
    }
    return d;
}
const Normal3f &ParamSet::FindOneNormal(const std::string &name, const Normal3f &d) const {
    const auto *item = FindItemConst(normals, name);
    if (item && item->nItems >= 1) {
        const_cast<ParamSetItem<Normal3f> *>(item)->lookedUp = true;
        return item->data[0];
    }
    return d;
}
const RGBColor &ParamSet::FindOneRGBColor(const std::string &name, const RGBColor &d) const {
    const auto *item = FindItemConst(spectra, name);
    if (item && item->nItems >= 1) {
        const_cast<ParamSetItem<RGBColor> *>(item)->lookedUp = true;
        return item->data[0];
    }
    return d;
}
const std::string &ParamSet::FindOneString(const std::string &name, const std::string &d) const {
    const auto *item = FindItemConst(strings, name);
    if (item && item->nItems >= 1) {
        const_cast<ParamSetItem<std::string> *>(item)->lookedUp = true;
        return item->data[0];
    }
    return d;
}

// Find (array) methods.
const float *ParamSet::FindFloat(const std::string &name, std::uint32_t *nItems) const {
    const auto *item = FindItemConst(floats, name);
    if (item) {
        const_cast<ParamSetItem<float> *>(item)->lookedUp = true;
        *nItems = item->nItems;
        return item->data;
    }
    *nItems = 0;
    return nullptr;
}
const int *ParamSet::FindInt(const std::string &name, std::uint32_t *nItems) const {
    const auto *item = FindItemConst(ints, name);
    if (item) {
        const_cast<ParamSetItem<int> *>(item)->lookedUp = true;
        *nItems = item->nItems;
        return item->data;
    }
    *nItems = 0;
    return nullptr;
}
const bool *ParamSet::FindBool(const std::string &name, std::uint32_t *nItems) const {
    const auto *item = FindItemConst(bools, name);
    if (item) {
        const_cast<ParamSetItem<bool> *>(item)->lookedUp = true;
        *nItems = item->nItems;
        return item->data;
    }
    *nItems = 0;
    return nullptr;
}
const Point3f *ParamSet::FindPoint(const std::string &name, std::uint32_t *nItems) const {
    const auto *item = FindItemConst(points, name);
    if (item) {
        const_cast<ParamSetItem<Point3f> *>(item)->lookedUp = true;
        *nItems = item->nItems;
        return item->data;
    }
    *nItems = 0;
    return nullptr;
}
const Vector3f *ParamSet::FindVector(const std::string &name, std::uint32_t *nItems) const {
    const auto *item = FindItemConst(vectors, name);
    if (item) {
        const_cast<ParamSetItem<Vector3f> *>(item)->lookedUp = true;
        *nItems = item->nItems;
        return item->data;
    }
    *nItems = 0;
    return nullptr;
}
const Normal3f *ParamSet::FindNormal(const std::string &name, std::uint32_t *nItems) const {
    const auto *item = FindItemConst(normals, name);
    if (item) {
        const_cast<ParamSetItem<Normal3f> *>(item)->lookedUp = true;
        *nItems = item->nItems;
        return item->data;
    }
    *nItems = 0;
    return nullptr;
}
const RGBColor *ParamSet::FindRGBColor(const std::string &name, std::uint32_t *nItems) const {
    const auto *item = FindItemConst(spectra, name);
    if (item) {
        const_cast<ParamSetItem<RGBColor> *>(item)->lookedUp = true;
        *nItems = item->nItems;
        return item->data;
    }
    *nItems = 0;
    return nullptr;
}
const std::string *ParamSet::FindString(const std::string &name, std::uint32_t *nItems) const {
    const auto *item = FindItemConst(strings, name);
    if (item) {
        const_cast<ParamSetItem<std::string> *>(item)->lookedUp = true;
        *nItems = item->nItems;
        return item->data;
    }
    *nItems = 0;
    return nullptr;
}

// FindTexture.
std::string ParamSet::FindTexture(const std::string &name) const {
    const auto *item = FindItemConst(textures, name);
    if (item && item->nItems >= 1) {
        const_cast<ParamSetItem<std::string> *>(item)->lookedUp = true;
        return item->data[0];
    }
    return "";
}

// Housekeeping.
void ParamSet::MarkAllUsed() const {
    auto mark = [](auto &v) {
        for (auto *item : v)
            item->lookedUp = true;
    };
    mark(ints); mark(bools); mark(floats); mark(points);
    mark(vectors); mark(normals); mark(spectra); mark(strings); mark(textures);
}

void ParamSet::ReportUnused() const {
    auto report = [this](const auto &v) {
        for (const auto *item : v)
            if (!item->lookedUp)
                LOG(LUX_WARNING) << "Unused parameter \"" << item->name << "\".";
    };
    report(ints); report(bools); report(floats); report(points);
    report(vectors); report(normals); report(spectra); report(strings); report(textures);
}

void ParamSet::Clear() {
    auto clear = [](auto &v) {
        for (auto *item : v) delete item;
        v.clear();
    };
    clear(ints); clear(bools); clear(floats); clear(points);
    clear(vectors); clear(normals); clear(spectra); clear(strings); clear(textures);
}

std::string ParamSet::ToString() const {
    std::ostringstream oss;
    auto dump = [&oss](const char *type, const auto &v) {
        for (const auto *item : v)
            oss << type << " " << item->name << " [" << item->nItems << " values]\n";
    };
    dump("int", ints);
    dump("bool", bools);
    dump("float", floats);
    dump("point", points);
    dump("vector", vectors);
    dump("normal", normals);
    dump("color", spectra);
    dump("string", strings);
    dump("texture", textures);
    return oss.str();
}

ParamSet::~ParamSet() {
    Clear();
}

// Clone every owned item so the copy owns independent data.
namespace {
template <class T>
std::vector<ParamSetItem<T> *> CloneItems(const std::vector<ParamSetItem<T> *> &v) {
    std::vector<ParamSetItem<T> *> out;
    out.reserve(v.size());
    for (const auto *item : v)
        out.push_back(item->Clone());
    return out;
}
} // namespace

ParamSet::ParamSet(const ParamSet &p)
    : ints(CloneItems(p.ints)),
      bools(CloneItems(p.bools)),
      floats(CloneItems(p.floats)),
      points(CloneItems(p.points)),
      vectors(CloneItems(p.vectors)),
      normals(CloneItems(p.normals)),
      spectra(CloneItems(p.spectra)),
      strings(CloneItems(p.strings)),
      textures(CloneItems(p.textures)) {
}

ParamSet &ParamSet::operator=(const ParamSet &p) {
    if (this != &p) {
        Clear();
        ints = CloneItems(p.ints);
        bools = CloneItems(p.bools);
        floats = CloneItems(p.floats);
        points = CloneItems(p.points);
        vectors = CloneItems(p.vectors);
        normals = CloneItems(p.normals);
        spectra = CloneItems(p.spectra);
        strings = CloneItems(p.strings);
        textures = CloneItems(p.textures);
    }
    return *this;
}

} // namespace lux2
