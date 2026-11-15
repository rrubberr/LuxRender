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

// Path clone / truncate / vertex-copy over the wavefront arena.

#ifndef LUX2_INTEGRATORS_ERPT_PATH_OPS_H
#define LUX2_INTEGRATORS_ERPT_PATH_OPS_H

#include "integrators/wf/storage.h"

#include <cstring>

namespace lux2
{
    namespace erpt
    {
        using wf::PathStore;

        // Copy vertex `v` from (srcPs,srcPath) to (dstPs,dstPath).
        inline void CopyVertex(PathStore &dstPs, uint32_t dstPath,
                               const PathStore &srcPs, uint32_t srcPath,
                               uint32_t v)
        {
            const uint32_t srec = srcPath * srcPs.arena->stride + v;
            const uint32_t drec = dstPath * dstPs.arena->stride + v;
            std::memmove(&dstPs.arena->geomWords[size_t(drec) * wf::GEOM_WORDS],
                         &srcPs.arena->geomWords[size_t(srec) * wf::GEOM_WORDS],
                         sizeof(uint32_t) * wf::GEOM_WORDS);
            std::memmove(&dstPs.arena->misWords[size_t(drec) * wf::MIS_WORDS],
                         &srcPs.arena->misWords[size_t(srec) * wf::MIS_WORDS],
                         sizeof(uint32_t) * wf::MIS_WORDS);
        }

        // Copy vertices [lo, hi] from (srcPs,srcPath) to (dstPs,dstPath).
        inline void CopySpan(PathStore &dstPs, uint32_t dstPath,
                             const PathStore &srcPs, uint32_t srcPath,
                             uint32_t lo, uint32_t hi)
        {
            const uint32_t n = srcPs.n[srcPath];
            if (n == 0u || lo >= n || lo > hi)
                return;
            if (hi >= n)
                hi = n - 1u;
            for (uint32_t v = lo; v <= hi; ++v)
                CopyVertex(dstPs, dstPath, srcPs, srcPath, v);
        }

    } // namespace erpt
} // namespace lux2

#endif // LUX2_INTEGRATORS_ERPT_PATH_OPS_H
