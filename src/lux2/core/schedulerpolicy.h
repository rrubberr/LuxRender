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

#ifndef LUX2_SCHEDULERPOLICY_H
#define LUX2_SCHEDULERPOLICY_H

#include <atomic>
#include <cstdint>
#include <vector>

namespace lux2
{

    // A unit of work handed to the scheduler to advance tile by count
    // samples this visit.
    struct WorkItem
    {
        uint32_t tile;
        uint32_t count;
    };

    // Read only view of tile scheduling state to decide where to feed.
    // committed[t] is samples already drawn for the tile by the seed basis cursor.
    // inFlight[t] is items queued but not yet finished.
    struct TileState
    {
        std::vector<std::atomic<uint32_t>> *committed;
        std::vector<std::atomic<uint32_t>> *inFlight;
        int tileCount = 0;
        uint32_t baseCount = 16; // tileSpp
        int haltSpp = 0;         // <= 0 disables the spp clamp
    };

    // Given current tile loads, choose the next item to feed
    // and bump its inFlight, or decline to keep feeding.
    template <class Feeder>
    class BackfillPolicy
    {
    public:
        virtual ~BackfillPolicy() = default;
        virtual void Refill(Feeder &feeder, const TileState &state) = 0;
    };

    // Uniform samplers feed the tile with the fewest committed + in-flight
    // samples so tiles converge together. Batches are clamped to not
    // overshoot halt spp.
    template <class Feeder>
    class UniformBackfill final : public BackfillPolicy<Feeder>
    {
    public:
        void Refill(Feeder &feeder, const TileState &state) override
        {
            uint32_t best = 0;
            uint64_t bestLoad = UINT64_MAX;
            for (int t = 0; t < state.tileCount; ++t)
            {
                const uint64_t load =
                    uint64_t((*state.committed)[t].load(std::memory_order_relaxed)) +
                    (*state.inFlight)[t].load(std::memory_order_relaxed);
                if (load < bestLoad)
                {
                    bestLoad = load;
                    best = uint32_t(t);
                }
            }

            uint32_t count = state.baseCount;
            if (state.haltSpp > 0)
            {
                const uint64_t room =
                    uint64_t(state.haltSpp) -
                    std::min(uint64_t(state.haltSpp), bestLoad);
                if (room == 0)
                    return; // every tile is complete, stop feeding
                count = uint32_t(std::min<uint64_t>(count, room));
            }

            (*state.inFlight)[best].fetch_add(1, std::memory_order_relaxed);
            feeder.add(WorkItem{best, count});
        }
    };

} // namespace lux2

#endif // LUX2_SCHEDULERPOLICY_H
