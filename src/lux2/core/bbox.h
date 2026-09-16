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

#ifndef LUX2_BBOX_H
#define LUX2_BBOX_H

// Axis-aligned bounding box, luxrays::BBox idiom (pMin/pMax, Inside,
// Overlaps, Expand, Volume, SurfaceArea, MaximumExtent, Center, Union).

#include "core/geometry.h"

#include <limits>

namespace lux2 {

class BBox {
public:
    // An invalid box: pMin=+inf, pMax=-inf (the empty set).
    BBox() {
        pMin = Point3f( std::numeric_limits<Float>::infinity());
        pMax = Point3f(-std::numeric_limits<Float>::infinity());
    }

    BBox(const Point3f &p) : pMin(p), pMax(p) { }

    BBox(const Point3f &p1, const Point3f &p2) {
        pMin = Point3f(enoki::min(p1.x(), p2.x()),
                       enoki::min(p1.y(), p2.y()),
                       enoki::min(p1.z(), p2.z()));
        pMax = Point3f(enoki::max(p1.x(), p2.x()),
                       enoki::max(p1.y(), p2.y()),
                       enoki::max(p1.z(), p2.z()));
    }

    bool IsValid() const {
        return (pMin.x() <= pMax.x()) && (pMin.y() <= pMax.y()) &&
               (pMin.z() <= pMax.z());
    }

    bool Inside(const Point3f &pt) const {
        return (pt.x() >= pMin.x() && pt.x() <= pMax.x() &&
                pt.y() >= pMin.y() && pt.y() <= pMax.y() &&
                pt.z() >= pMin.z() && pt.z() <= pMax.z());
    }

    bool Inside(const BBox &bb) const {
        return (bb.pMin.x() >= pMin.x() && bb.pMax.x() <= pMax.x() &&
                bb.pMin.y() >= pMin.y() && bb.pMax.y() <= pMax.y() &&
                bb.pMin.z() >= pMin.z() && bb.pMax.z() <= pMax.z());
    }

    bool Overlaps(const BBox &b) const {
        const bool x = (pMax.x() >= b.pMin.x()) && (pMin.x() <= b.pMax.x());
        const bool y = (pMax.y() >= b.pMin.y()) && (pMin.y() <= b.pMax.y());
        const bool z = (pMax.z() >= b.pMin.z()) && (pMin.z() <= b.pMax.z());
        return (x && y && z);
    }

    void Expand(Float delta) {
        pMin -= Vector3f(delta, delta, delta);
        pMax += Vector3f(delta, delta, delta);
    }

    Float Volume() const {
        const Vector3f d = pMax - pMin;
        return d.x() * d.y() * d.z();
    }

    Float SurfaceArea() const {
        const Vector3f d = pMax - pMin;
        return 2.f * (d.x() * d.y() + d.y() * d.z() + d.z() * d.x());
    }

    // Axis index with the largest extent.
    int MaximumExtent() const {
        const Vector3f diag = pMax - pMin;
        if (diag.x() > diag.y() && diag.x() > diag.z())
            return 0;
        else if (diag.y() > diag.z())
            return 1;
        else
            return 2;
    }

    Point3f Center() const { return (pMin + pMax) * .5f; }

    Vector3f Diagonal() const { return pMax - pMin; }

    friend BBox Union(const BBox &b, const Point3f &p) {
        BBox r;
        r.pMin = Point3f(enoki::min(b.pMin.x(), p.x()),
                         enoki::min(b.pMin.y(), p.y()),
                         enoki::min(b.pMin.z(), p.z()));
        r.pMax = Point3f(enoki::max(b.pMax.x(), p.x()),
                         enoki::max(b.pMax.y(), p.y()),
                         enoki::max(b.pMax.z(), p.z()));
        return r;
    }

    friend BBox Union(const BBox &b, const BBox &b2) {
        BBox r;
        r.pMin = Point3f(enoki::min(b.pMin.x(), b2.pMin.x()),
                         enoki::min(b.pMin.y(), b2.pMin.y()),
                         enoki::min(b.pMin.z(), b2.pMin.z()));
        r.pMax = Point3f(enoki::max(b.pMax.x(), b2.pMax.x()),
                         enoki::max(b.pMax.y(), b2.pMax.y()),
                         enoki::max(b.pMax.z(), b2.pMax.z()));
        return r;
    }

    // BBox Public Data.
    Point3f pMin, pMax;
};

} // namespace lux2

#endif // LUX2_BBOX_H
