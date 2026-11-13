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

#ifndef LUX2_EMBREE2_H
#define LUX2_EMBREE2_H

#include "core/math/ray.h"
#include "core/math/geometry.h"
#include "core/math/bbox.h"
#include "core/memory/huge_alloc.h"

#include <embree4/rtcore.h>

#include <cstdint>
#include <vector>

namespace lux2
{

    struct SceneDescription;
    struct MeshDesc;

    // Packed triangle shading record.
    constexpr std::uint32_t TRI_WORDS = 32;
    constexpr std::uint32_t kTri_v0 = 0;  // +0..2  x,y,z
    constexpr std::uint32_t kTri_v1 = 3;  // +0..2
    constexpr std::uint32_t kTri_v2 = 6;  // +0..2
    constexpr std::uint32_t kTri_n0 = 9;  // +0..2
    constexpr std::uint32_t kTri_n1 = 12; // +0..2
    constexpr std::uint32_t kTri_n2 = 15; // +0..2
    constexpr std::uint32_t kTri_uv0 = 18; // +0..1 u,v
    constexpr std::uint32_t kTri_uv1 = 20; // +0..1
    constexpr std::uint32_t kTri_uv2 = 22; // +0..1
    constexpr std::uint32_t kTri_matID = 24;
    constexpr std::uint32_t kTri_lightID = 25;
    constexpr std::uint32_t kTri_medFront = 26;
    constexpr std::uint32_t kTri_medBack = 27;
    // Words 28..31 reserved.

    // Traversal hint. Coherent = camera primaries and environment light
    // importance samples. Incoherent = diffuse secondaries, shadow rays.
    enum class Coherent : bool
    {
        No = false,
        Yes = true
    };

    // The Embree device/scene and tables.
    class EmbreeScene
    {
    public:
        EmbreeScene() = default;
        ~EmbreeScene();

        EmbreeScene(const EmbreeScene &) = delete;
        EmbreeScene &operator=(const EmbreeScene &) = delete;

        // Build device + scene + tables from tessellated meshes in desc.
        void Build(SceneDescription &desc);

        // Build directly from a vector of worldspace meshes.
        void BuildFromMeshes(const std::vector<MeshDesc> &meshes);

        // Packet intersect.
        void Intersect(RayP &ray, HitP &hit,
                       Coherent hint = Coherent::No) const;

        // Packet occlusion test.
        MaskP Occluded(const RayP &ray, MaskP active,
                       Coherent hint = Coherent::No) const;

        // Fill from the tables. ShadeHit uses the transposed record load.
        void ShadeHit(HitP &hit) const;
        void ShadeHit_gather(HitP &hit) const;

        // Medium id containing the world point.
        std::uint32_t MediumIDAt(const Point3f &p) const;

        BBox WorldBound() const { return m_bound; }
        size_t TriangleCount() const { return m_triCount; }

    private:
        static RTCDevice AcquireDevice();

        RTCDevice m_device = nullptr; // borrowed
        RTCScene m_scene = nullptr;   // owned

        size_t m_triCount = 0;
        BBox m_bound;

        // Global triangle index -> triRecords[g * TRI_WORDS + word].
        HugeVector<std::uint32_t> triRecords;
        std::vector<std::uint32_t> geomToBase; // geomID -> base index
    };

} // namespace lux2

#endif // LUX2_EMBREE2_H
