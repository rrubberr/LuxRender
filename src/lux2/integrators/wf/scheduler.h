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

// Wavefront scheduler.

#ifndef LUX2_INTEGRATORS_WF_SCHEDULER_H
#define LUX2_INTEGRATORS_WF_SCHEDULER_H

#include "integrators/wf/sample_table.h"
#include "integrators/wf/storage.h"

#include "core/math/vecp.h"

#include <enoki/array.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace lux2
{
    namespace wf
    {
        enum class PathKind
        {
            Eye,
            Light
        };

        // A general connection waiting for a Gen packet.
        struct Pair
        {
            uint32_t ePath, lPath, sample, slot;
        };

        // ceil_log2 bucketing of general connection pairs by
        // max(nE,nL): {<=8, <=16, <=32, <=64, <=128}. Refill takes from
        // the fullest bin so MIS loops share a similar trip count.
        class PairQueues
        {
        public:
            static constexpr int kBins = 5;

            static int ClassOf(uint32_t nE, uint32_t nL)
            {
                const uint32_t m = std::max(nE, nL);
                if (m <= 8u)
                    return 0;
                if (m <= 16u)
                    return 1;
                if (m <= 32u)
                    return 2;
                if (m <= 64u)
                    return 3;
                return 4;
            }

            void Push(const Pair &p, uint32_t nE, uint32_t nL)
            {
                m_bins[ClassOf(nE, nL)].push_back(p);
            }

            bool Empty() const
            {
                for (int b = 0; b < kBins; ++b)
                    if (!m_bins[b].empty())
                        return false;
                return true;
            }
            uint32_t Queued() const
            {
                uint32_t n = 0;
                for (int b = 0; b < kBins; ++b)
                    n += uint32_t(m_bins[b].size());
                return n;
            }
            bool BinEmpty(int b) const { return m_bins[b].empty(); }
            uint32_t BinSize(int b) const { return uint32_t(m_bins[b].size()); }

            // Bin with the most pairs (-1 if all empty).
            int FullestBin() const
            {
                int best = -1;
                uint32_t bestN = 0;
                for (int b = 0; b < kBins; ++b)
                {
                    const uint32_t n = uint32_t(m_bins[b].size());
                    if (n > bestN)
                    {
                        bestN = n;
                        best = b;
                    }
                }
                return best;
            }

            // PACKET_WIDTH pairs from bin b into arrays. Returns the count.
            uint32_t FillPacket(int b, UInt32P &ePath, UInt32P &lPath,
                                UInt32P &sample, UInt32P &slot)
            {
                std::vector<Pair> &q = m_bins[b];
                alignas(64) uint32_t eA[PACKET_WIDTH], lA[PACKET_WIDTH],
                    sA[PACKET_WIDTH], slA[PACKET_WIDTH];
                const uint32_t take =
                    uint32_t(std::min<size_t>(q.size(), PACKET_WIDTH));
                for (uint32_t i = 0; i < take; ++i)
                {
                    const Pair &p = q.back();
                    q.pop_back();
                    eA[i] = p.ePath;
                    lA[i] = p.lPath;
                    sA[i] = p.sample;
                    slA[i] = p.slot;
                }
                // Tail lanes point at slot 0.
                for (uint32_t i = take; i < PACKET_WIDTH; ++i)
                {
                    eA[i] = 0u;
                    lA[i] = 0u;
                    sA[i] = 0u;
                    slA[i] = 0u;
                }
                ePath = enoki::load_unaligned<UInt32P>(eA);
                lPath = enoki::load_unaligned<UInt32P>(lA);
                sample = enoki::load_unaligned<UInt32P>(sA);
                slot = enoki::load_unaligned<UInt32P>(slA);
                return take;
            }

        private:
            std::vector<Pair> m_bins[kBins];
        };

        // A ready queue (NEE/CamNee keyed by sample, Cam by light path).
        struct WorkQueue
        {
            std::vector<uint32_t> items;

            void Push(uint32_t i) { items.push_back(i); }
            bool Empty() const { return items.empty(); }
            uint32_t Size() const { return uint32_t(items.size()); }

            // PACKET_WIDTH keys into `out`. Returns the count.
            uint32_t FillPacket(UInt32P &out)
            {
                alignas(64) uint32_t a[PACKET_WIDTH];
                const uint32_t take =
                    uint32_t(std::min<size_t>(items.size(), PACKET_WIDTH));
                for (uint32_t i = 0; i < take; ++i)
                {
                    a[i] = items.back();
                    items.pop_back();
                }
                for (uint32_t i = take; i < PACKET_WIDTH; ++i)
                    a[i] = 0u;
                out = enoki::load_unaligned<UInt32P>(a);
                return take;
            }
        };

        // Sample/path lifecycle.
        struct Scheduler
        {
            SampleTable &tbl;
            const PathStore &eyePs;
            const PathStore &lightPs;
            bool useT1 = false;

            PairQueues gen;
            WorkQueue nee, camNee, cam;

            // Vertex count per path.
            std::vector<uint32_t> eyeN;

            Scheduler(SampleTable &t, const PathStore &ePs, const PathStore &lPs,
                      bool useT1_)
                : tbl(t), eyePs(ePs), lightPs(lPs), useT1(useT1_)
            {
                eyeN.assign(ePs.n.size(), 0u);
            }

            // Route the work a drained path unblocks.
            void OnEyeDone(uint32_t sample, uint32_t path)
            {
                tbl.eyePath[sample] = path;
                tbl.eyeDone[sample] = 1u;
                eyeN[path] = eyePs.n[path];
                nee.Push(sample);
                if (useT1)
                    camNee.Push(sample);
                const uint32_t nE = eyePs.n[path];
                for (uint32_t j = 0; j < tbl.nT; ++j)
                {
                    if (!tbl.LightDone(sample, j))
                        continue;
                    const uint32_t lp = tbl.LightPath(sample, j);
                    // A cancelled entry has no path and already had
                    // its Gen settled in CancelLight, skip.
                    if (lp == LUX_INVALID_PATH)
                        continue;
                    const uint32_t nL = lightPs.n[lp];
                    if (nE >= 2u && nL >= 1u)
                        gen.Push({path, lp, sample, slotOf(sample, j)}, nE, nL);
                    else
                        CompleteWork(sample); // pair cannot connect
                }
            }

            // A light list entry that cannot spawn.
            void CancelLight(uint32_t sample, uint32_t j)
            {
                tbl.SetLightDone(sample, j);
                CompleteWork(sample, 1u + (useT1 ? 1u : 0u));
            }

            void OnLightDone(uint32_t sample, uint32_t path, uint32_t j)
            {
                tbl.SetLightDone(sample, j);
                if (useT1)
                    cam.Push(path);
                if (tbl.eyeDone[sample])
                {
                    const uint32_t nE = eyePs.n[tbl.eyePath[sample]];
                    const uint32_t nL = lightPs.n[path];
                    if (nE >= 2u && nL >= 1u)
                        gen.Push({tbl.eyePath[sample], path, sample,
                                  slotOf(sample, j)},
                                 nE, nL);
                    else
                        CompleteWork(sample); // pair cannot connect
                }
            }

            // Dispatch.
            void OnPathDone(PathKind k, uint32_t sample, uint32_t path)
            {
                if (k == PathKind::Eye)
                    OnEyeDone(sample, path);
                else
                    OnLightDone(sample, path, IndexOf(sample, path));
            }

            // Samples whose pending reached 0, awaiting retire.
            std::vector<uint32_t> retireQueue;

            // Decrement a sample's work count; on reaching 0 queue
            // retire and return true.
            bool CompleteWork(uint32_t sample, uint32_t n = 1)
            {
                tbl.pending[sample] -= n;
                if (tbl.pending[sample] == 0u)
                {
                    retireQueue.push_back(sample);
                    return true;
                }
                return false;
            }

            // Completion for a connector packet.
            void CompleteSample(const UInt32P &samples, uint32_t i)
            {
                alignas(64) uint32_t sA[PACKET_WIDTH];
                enoki::store_unaligned(sA, samples);
                CompleteWork(sA[i]);
            }

        private:
            // The light slot stored for list entry j.
            uint32_t slotOf(uint32_t /*sample*/, uint32_t j) const { return j; }

            // Find the list index j whose stored path id == path (nT is small).
            uint32_t IndexOf(uint32_t sample, uint32_t path) const
            {
                for (uint32_t j = tbl.nT; j-- > 0;)
                    if (tbl.LightPath(sample, j) == path)
                        return j;
                return 0u;
            }
        };

    } // namespace wf
} // namespace lux2

#endif // LUX2_INTEGRATORS_WF_SCHEDULER_H
