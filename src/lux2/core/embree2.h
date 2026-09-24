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

#include "core/ray.h"
#include "core/geometry.h"
#include "core/bbox.h"

#include <embree4/rtcore.h>

#include <cstdint>
#include <vector>

namespace lux2
{

    struct SceneDescription;
    struct MeshDesc;

    // Traversal hint. Coherent = camera primaries and env-light
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

        // Fill hit.p/ngeo/sh_n/uv/matID/lightID from the tables.
        void ShadeHit(const RayP &ray, HitP &hit) const;

        BBox WorldBound() const { return m_bound; }
        size_t TriangleCount() const { return m_triCount; }

    private:
        static RTCDevice AcquireDevice();

        RTCDevice m_device = nullptr; // borrowed
        RTCScene m_scene = nullptr;   // owned

        size_t m_triCount = 0;
        BBox m_bound;

        // Global triangle index.
        std::vector<float> v0x, v0y, v0z;
        std::vector<float> v1x, v1y, v1z;
        std::vector<float> v2x, v2y, v2z;
        std::vector<float> n0x, n0y, n0z;
        std::vector<float> n1x, n1y, n1z;
        std::vector<float> n2x, n2y, n2z;
        std::vector<float> uv0u, uv0v, uv1u, uv1v, uv2u, uv2v;
        std::vector<std::uint32_t> matID;
        std::vector<std::int32_t> lightID;
        std::vector<std::uint32_t> geomToBase; // geomID -> base index
    };

} // namespace lux2

#endif // LUX2_EMBREE2_H
