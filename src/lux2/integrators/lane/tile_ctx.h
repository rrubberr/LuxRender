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

// Tile constant context shared by every lane integrator. One Build() replaces
// the per-integrator setup that path and bidirectional carried separately.

#ifndef LUX2_INTEGRATORS_LANE_TILE_CTX_H
#define LUX2_INTEGRATORS_LANE_TILE_CTX_H

#include "integrators/lane/keys.h"
#include "integrators/lane/occlusion.h"

#include "core/math/vecp.h"
#include "core/renderinghints.h"

#include <cstdint>
#include <string>
#include <vector>

namespace lux2
{
    class BsdfPtrTable;
    class Camera;
    class EmbreeScene;
    class Light;
    class MediumPtrTable;
    class Scene;

    // What Build() needs beyond the scene. The prebuilt per-group tables come
    // from Start(); when RenderTile runs without Start() (a scene with no
    // light groups) Build falls back to the named strategies.
    struct TileCtxOptions
    {
        // Per-group tables built once at Start(), indexed by group.
        const std::vector<LightsSamplingStrategy> *prebuiltD = nullptr;
        const std::vector<LightsSamplingStrategy> *prebuiltP = nullptr;
        // Strategy names used to build a fallback table.
        const std::string *directStrategy = nullptr;
        const std::string *pathStrategy = nullptr;
    };

    // Immutable after Build(); passed by const reference. Not copyable: lsD and
    // lsP may address the owned fallback tables, so a copy would alias the
    // original's storage.
    struct TileCtx
    {
        TileCtx() = default;
        TileCtx(const TileCtx &) = delete;
        TileCtx &operator=(const TileCtx &) = delete;

        const EmbreeScene *embree = nullptr;
        const BsdfPtrTable *table = nullptr;
        const MediumPtrTable *mediumTable = nullptr;
        bool hasMedia = false;
        // Occlusion inputs; hasMedia/hasNull decide the fast path.
        ShadowCtx shadow;

        // Active lightgroup filtering.
        int activeGroup = 0;
        bool multiGroup = false;

        // Lights of the active group, indexed by light id within the group.
        std::vector<const Light *> lightPtrs;
        std::vector<float> lightInf;
        // Medium enclosing each light's emission point.
        std::vector<std::uint32_t> lightMedium;
        int nLights = 0;

        // Area lights: by area id, and by lightPtrs index.
        std::vector<const Light *> areaPtrs;
        std::vector<std::int32_t> areaGroup;
        std::vector<float> areaById;
        std::vector<float> areaByIdx;

        // Sampling tables for the active group. lsD drives NEE and direct
        // densities; lsP drives light subpath origins (bidirectional only).
        const LightsSamplingStrategy *lsD = nullptr;
        const LightsSamplingStrategy *lsP = nullptr;

      private:
        // Owned fallback tables; lsD/lsP point here when Start() never ran.
        LightsSamplingStrategy m_fallbackD, m_fallbackP;

        friend void BuildTileCtx(const Scene &, const TileCtxOptions &, TileCtx &);
    };

    // Fill everything derivable from the scene: group filtering, light and area
    // tables, media/Null flags, occlusion inputs and the strategy selection.
    // Integrator-specific options are assigned by the caller afterwards.
    void BuildTileCtx(const Scene &scene, const TileCtxOptions &opts, TileCtx &out);

} // namespace lux2

#endif // LUX2_INTEGRATORS_LANE_TILE_CTX_H
