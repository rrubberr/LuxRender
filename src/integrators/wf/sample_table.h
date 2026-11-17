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

// Sample accumulator table for wavefront integrators.

#ifndef LUX2_INTEGRATORS_WF_SAMPLE_TABLE_H
#define LUX2_INTEGRATORS_WF_SAMPLE_TABLE_H

#include "integrators/wf/storage.h" // LUX_INVALID_PATH

#include "core/math/vecp.h"

#include <enoki/array.h>

#include <cstdint>
#include <vector>

namespace lux2
{
    namespace wf
    {
        // One row per inflight sample slot.
        struct SampleTable
        {
            std::vector<float> x, y;       // film position
            std::vector<float> alpha;      // coverage (0 = background = miss)
            std::vector<float> camWeight;  // camera throughput
            std::vector<float> L;          // accumulated radiance (SWCSpectrumP)
            std::vector<float> w;          // sampled wavelength
            std::vector<uint32_t> pending; // connections still owed
            std::vector<uint32_t> seed;    // sampler stream key
            std::vector<uint32_t> sidx;    // sample index
            // Solid angle density of the primary ray.
            std::vector<float> camPdfR0;

            std::vector<uint32_t> eyePath;   // eye pool path id for this sample
            std::vector<uint8_t> eyeDone;    // eye walker finished this sample
            std::vector<uint32_t> nextLight; // light paths spawned so far
            std::vector<uint32_t> freeList;  // LIFO of free slot ids

            // A light path id is stored when the walker starts and marked done when the
            // walker drains the path.
            uint32_t nT = 1;
            std::vector<uint32_t> lightPathsFlat;
            std::vector<uint8_t> lightDoneFlat;

            void Init(uint32_t nSlots, uint32_t taskCount = 1)
            {
                nT = taskCount ? taskCount : 1u;
                x.assign(nSlots, 0.f);
                y.assign(nSlots, 0.f);
                alpha.assign(nSlots, 0.f);
                camWeight.assign(nSlots, 0.f);
                L.assign(nSlots, 0.f);
                w.assign(nSlots, 0.f);
                pending.assign(nSlots, 0u);
                seed.assign(nSlots, 0u);
                sidx.assign(nSlots, 0u);
                camPdfR0.assign(nSlots, 0.f);
                eyePath.assign(nSlots, LUX_INVALID_PATH);
                eyeDone.assign(nSlots, 0u);
                nextLight.assign(nSlots, 0u);
                lightPathsFlat.assign(size_t(nSlots) * nT, LUX_INVALID_PATH);
                lightDoneFlat.assign(size_t(nSlots) * nT, 0u);
                freeList.clear();
                for (uint32_t s = nSlots; s-- > 0;)
                    freeList.push_back(s);
            }

            // Slot allocator.
            uint32_t AcquireSlot()
            {
                if (freeList.empty())
                    return LUX_INVALID_PATH;
                const uint32_t s = freeList.back();
                freeList.pop_back();
                pending[s] = 0u;
                eyeDone[s] = 0u;
                nextLight[s] = 0u;
                eyePath[s] = LUX_INVALID_PATH;
                L[s] = 0.f;
                for (uint32_t j = 0; j < nT; ++j)
                {
                    lightPathsFlat[size_t(s) * nT + j] = LUX_INVALID_PATH;
                    lightDoneFlat[size_t(s) * nT + j] = 0u;
                }
                return s;
            }
            void ReleaseSlot(uint32_t s) { freeList.push_back(s); }
            bool HasFreeSlot() const { return !freeList.empty(); }

            // Path accessors (j in [0, nT)).
            uint32_t LightPath(uint32_t s, uint32_t j) const
            {
                return lightPathsFlat[size_t(s) * nT + j];
            }
            void SetLightPath(uint32_t s, uint32_t j, uint32_t p)
            {
                lightPathsFlat[size_t(s) * nT + j] = p;
            }
            bool LightDone(uint32_t s, uint32_t j) const
            {
                return lightDoneFlat[size_t(s) * nT + j] != 0u;
            }
            void SetLightDone(uint32_t s, uint32_t j)
            {
                lightDoneFlat[size_t(s) * nT + j] = 1u;
            }
        };

        // L[s] += x for masked lanes.
        inline void AddToSample(float *L, const UInt32P &s, const FloatP &x,
                                const MaskP &m)
        {
            alignas(64) uint32_t si[PACKET_WIDTH];
            alignas(64) float xv[PACKET_WIDTH], mk[PACKET_WIDTH];
            enoki::store_unaligned(si, s);
            enoki::store_unaligned(xv, x);
            enoki::store_unaligned(mk, select(m, FloatP(1.f), FloatP(0.f)));
            for (size_t i = 0; i < PACKET_WIDTH; ++i)
                if (mk[i] > 0.f)
                    L[si[i]] += xv[i];
        }

    } // namespace wf
} // namespace lux2

#endif // LUX2_INTEGRATORS_WF_SAMPLE_TABLE_H
