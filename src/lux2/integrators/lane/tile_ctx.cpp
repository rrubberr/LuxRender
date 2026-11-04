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

#include "integrators/lane/tile_ctx.h"

#include "core/accel/embree2.h"
#include "core/bsdfptr_table.h"
#include "core/light.h"
#include "core/mediumptr_table.h"
#include "core/scene.h"

#include <algorithm>

namespace lux2
{

    void BuildTileCtx(const Scene &scene, const TileCtxOptions &opts, TileCtx &out)
    {
        out.embree = scene.GetEmbree();
        out.table = &scene.GetBsdfTable();
        out.mediumTable = &scene.GetMediumTable();
        out.hasMedia = scene.HasMedia();
        out.shadow.embree = out.embree;
        out.shadow.table = out.table;
        out.shadow.mediumTable = out.mediumTable;
        out.shadow.hasMedia = out.hasMedia;
        out.shadow.hasNull = scene.HasNull();

        // Active lightgroup filtering.
        out.activeGroup = scene.GetActiveGroup();
        out.multiGroup = scene.LightGroupCount() > 1;
        out.lightPtrs = scene.GroupLights(out.activeGroup);
        out.lightInf = scene.GroupLightInf(out.activeGroup);
        out.nLights = static_cast<int>(out.lightPtrs.size());

        // Sampling tables indexed by the active light group. Without a matching
        // prebuilt table (a caller that runs RenderTile without Start()) build
        // one into the context, which owns it for the tile's lifetime.
        const bool haveD = opts.prebuiltD && out.activeGroup >= 0 &&
                           out.activeGroup < static_cast<int>(opts.prebuiltD->size());
        if (haveD)
            out.lsD = &(*opts.prebuiltD)[size_t(out.activeGroup)];
        else
        {
            out.m_fallbackD = CreateLightsSamplingStrategy(
                opts.directStrategy ? *opts.directStrategy : std::string(),
                out.lightPtrs, out.activeGroup, scene, LightRole::Direct);
            out.lsD = &out.m_fallbackD;
        }

        // The light-subpath table is only built for integrators that ask for
        // it, so path pays nothing for a table it never reads.
        if (opts.prebuiltP)
        {
            if (out.activeGroup >= 0 &&
                out.activeGroup < static_cast<int>(opts.prebuiltP->size()))
            {
                out.lsP = &(*opts.prebuiltP)[size_t(out.activeGroup)];
            }
            else if (opts.pathStrategy)
            {
                out.m_fallbackP = CreateLightsSamplingStrategy(
                    *opts.pathStrategy, out.lightPtrs, out.activeGroup, scene,
                    LightRole::LightPath);
                out.lsP = &out.m_fallbackP;
            }
        }

        // Area lights by id, and the area of each selected light by its index.
        const int nArea = scene.AreaLightCount();
        out.areaPtrs.reserve(size_t(nArea));
        out.areaById.assign(size_t(std::max(0, nArea)), 0.f);
        for (int a = 0; a < nArea; ++a)
        {
            const Light *al = scene.GetAreaLight(a);
            out.areaPtrs.push_back(al);
            out.areaById[size_t(a)] = al ? al->TotalArea() : 0.f;
        }
        if (out.multiGroup)
        {
            out.areaGroup.resize(size_t(nArea));
            for (int a = 0; a < nArea; ++a)
                out.areaGroup[a] = scene.AreaLightGroup(a);
        }
        out.areaByIdx.assign(size_t(std::max(0, out.nLights)), 0.f);
        for (int i = 0; i < out.nLights; ++i)
        {
            const Light *l = out.lightPtrs[size_t(i)];
            out.areaByIdx[size_t(i)] = (l && !l->IsInfinite()) ? l->TotalArea() : 0.f;
        }
    }

} // namespace lux2
