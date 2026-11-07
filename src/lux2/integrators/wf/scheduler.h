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

#include "integrators/debug/integrator_debug.h"
#include "integrators/wf/sample_table.h"
#include "integrators/wf/storage.h"

#include "core/math/vecp.h"

#include <enoki/array.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
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

        // One connection waiting in a ready queue.
        struct ConnItem
        {
            uint32_t ePath, lPath, sample, slot;
            int32_t t, s;
        };

        // Quantize an MIS loop trip count into levels.
        constexpr int kMisLevels = 8;
        inline int MisLevel(int v)
        {
            if (v <= 0)
                return 0;
            if (v <= 4)
                return v; // 1,2,3,4 exact
            if (v <= 6)
                return 5;
            if (v <= 9)
                return 6;
            return 7;
        }

        // Per-tile [t][s] -> bin lookup.
        struct MisBinTable
        {
            static constexpr int kBins = kMisLevels * kMisLevels; // 64
            uint8_t bin[(MAX_DEPTH + 1) * (MAX_DEPTH + 1)];
            // neigh[b] lists all bins ordered by distance in
            // (level_i, level_j), b first.
            uint8_t neigh[kBins][kBins];

            void Build(int eyeDepth, int lightDepth, bool useT1)
            {
                const int minEye = useT1 ? 1 : 2;
                for (int t = 0; t <= MAX_DEPTH; ++t)
                {
                    for (int s = 0; s <= MAX_DEPTH; ++s)
                    {
                        const int iMax = std::min(
                            t - minEye, lightDepth - std::min(lightDepth, s));
                        const int jMax =
                            std::min(s, eyeDepth - std::min(eyeDepth, t));
                        bin[t * (MAX_DEPTH + 1) + s] =
                            uint8_t(MisLevel(iMax) * kMisLevels +
                                    MisLevel(jMax));
                    }
                }
                for (int b = 0; b < kBins; ++b)
                {
                    const int bi = b / kMisLevels, bj = b % kMisLevels;
                    // Sort bin indices by distance to b.
                    uint8_t *ord = neigh[b];
                    for (int k = 0; k < kBins; ++k)
                        ord[k] = uint8_t(k);
                    std::sort(ord, ord + kBins, [&](uint8_t x, uint8_t y)
                              {
                                  const int dx =
                                      std::abs(x / kMisLevels - bi) +
                                      std::abs(x % kMisLevels - bj);
                                  const int dy =
                                      std::abs(y / kMisLevels - bi) +
                                      std::abs(y % kMisLevels - bj);
                                  if (dx != dy)
                                      return dx < dy;
                                  return x < y; });
                }
            }

            int At(int t, int s) const
            {
                if (t < 0)
                    t = 0;
                if (t > MAX_DEPTH)
                    t = MAX_DEPTH;
                if (s < 0)
                    s = 0;
                if (s > MAX_DEPTH)
                    s = MAX_DEPTH;
                return bin[t * (MAX_DEPTH + 1) + s];
            }
        };

        // Ready queue.
        class ConnQueues
        {
        public:
            static constexpr int kBins = MisBinTable::kBins;

            explicit ConnQueues(const MisBinTable *tbl) : m_tbl(tbl)
            {
                for (int i = 0; i <= kBins; ++i)
                    m_atLvl[i] = -1;
            }

            void Push(const ConnItem &it)
            {
                const int b = m_tbl->At(it.t, it.s);
                SetSize(b, uint32_t(m_bins[b].size()) + 1u);
                m_bins[b].push_back(it);
                ++m_items;
            }

            bool Empty() const { return m_items == 0u; }
            uint32_t Size() const { return m_items; }
            uint32_t BinSize(int b) const { return uint32_t(m_bins[b].size()); }

            // Bin with the most items.
            int FullestBin() const
            {
                if (m_lvl == 0ull)
                    return -1;
                const int lvl = 63 - __builtin_clzll(m_lvl);
                return m_atLvl[lvl];
            }

            // Fill up to PACKET_WIDTH items into the packet arrays.
            uint32_t Fill(int primary, UInt32P &ePath, UInt32P &lPath,
                          UInt32P &sample, UInt32P &slot, Int32P &t, Int32P &s,
                          bool topUp)
            {
                alignas(64) uint32_t eA[PACKET_WIDTH], lA[PACKET_WIDTH],
                    sA[PACKET_WIDTH], slA[PACKET_WIDTH];
                alignas(64) int32_t tA[PACKET_WIDTH], stA[PACKET_WIDTH];
                const int nOrd = topUp ? kBins : 1;
                const uint8_t *ord = m_tbl->neigh[primary];
                uint32_t k = 0;
                for (int oi = 0; oi < nOrd && k < PACKET_WIDTH; ++oi)
                {
                    const int b = ord[oi];
                    std::deque<ConnItem> &q = m_bins[b];
                    uint32_t popped = 0u;
                    while (!q.empty() && k < PACKET_WIDTH)
                    {
                        const ConnItem it = q.front();
                        q.pop_front();
                        eA[k] = it.ePath;
                        lA[k] = it.lPath;
                        sA[k] = it.sample;
                        slA[k] = it.slot;
                        tA[k] = it.t;
                        stA[k] = it.s;
                        ++k;
                        ++popped;
                    }
                    if (popped)
                        SetSize(b, uint32_t(q.size()));
                }
                m_items -= k;
                for (uint32_t i = k; i < PACKET_WIDTH; ++i)
                {
                    eA[i] = lA[i] = sA[i] = slA[i] = 0u;
                    tA[i] = stA[i] = 0;
                }
                ePath = enoki::load_unaligned<UInt32P>(eA);
                lPath = enoki::load_unaligned<UInt32P>(lA);
                sample = enoki::load_unaligned<UInt32P>(sA);
                slot = enoki::load_unaligned<UInt32P>(slA);
                t = enoki::load_unaligned<Int32P>(tA);
                s = enoki::load_unaligned<Int32P>(stA);
                return k;
            }

        private:
            // Record that bin `b` now holds `n` items.
            void SetSize(int b, uint32_t n)
            {
                const uint32_t old = m_binN[b];
                if (old == n)
                    return;
                // Move b out of `old`'s bookkeeping first.
                m_binN[b] = n;
                if (old > 0u && m_atLvl[old] == b)
                {
                    int rep = -1;
                    for (int i = 0; i < kBins; ++i)
                        if (m_binN[i] == old)
                        {
                            rep = i;
                            break;
                        }
                    m_atLvl[old] = rep;
                    if (rep < 0)
                        m_lvl &= ~(1ull << old);
                }
                if (n > 0u && m_atLvl[n] < 0)
                {
                    m_atLvl[n] = b;
                    m_lvl |= 1ull << n;
                }
            }

            std::deque<ConnItem> m_bins[kBins];
            const MisBinTable *m_tbl = nullptr;
            uint32_t m_items = 0u;
            // Incremental fullest bin state.
            uint32_t m_binN[kBins] = {0u};
            int m_atLvl[kBins + 1];
            uint64_t m_lvl = 0ull;
        };

        // Sample/path lifecycle.
        struct Scheduler
        {
            SampleTable &tbl;
            const PathStore &eyePs;
            const PathStore &lightPs;
            bool useT1 = false;

            const MisBinTable &bins;
            ConnQueues gen, nee, camNee, cam;

            // Vertex count per path.
            std::vector<uint32_t> eyeN;

            // Admission gate.
            std::vector<uint32_t> queuedCount;
            uint32_t gateSamples = 0u;

            Scheduler(SampleTable &t, const PathStore &ePs, const PathStore &lPs,
                      bool useT1_, const MisBinTable &bins_)
                : tbl(t), eyePs(ePs), lightPs(lPs), useT1(useT1_),
                  bins(bins_), gen(&bins_), nee(&bins_), camNee(&bins_),
                  cam(&bins_)
            {
                eyeN.assign(ePs.n.size(), 0u);
                queuedCount.assign(tbl.x.size(), 0u);
            }

            // Route the work a drained path unblocks.
            void OnEyeDone(uint32_t sample, uint32_t path)
            {
                tbl.eyePath[sample] = path;
                tbl.eyeDone[sample] = 1u;
                eyeN[path] = eyePs.n[path];
                const uint32_t nE = eyePs.n[path];

                // NEE instance.
                const uint32_t nCnt = nE >= 2u ? (nE - 1u) : 0u;
                ReserveItems(sample, nCnt);
                for (uint32_t k = 0; k < nCnt; ++k)
                    PushItem(nee, {path, 0u, sample, 0u, int32_t(2 + k), 1});

                // CamNee.
                if (useT1)
                    PushItem(camNee, {path, 0u, sample, 0u, 1, 1});

                for (uint32_t j = 0; j < tbl.nT; ++j)
                {
                    if (!tbl.LightDone(sample, j))
                        continue;
                    const uint32_t lp = tbl.LightPath(sample, j);
                    // A cancelled entry has no path and already had
                    // its Gen settled in CancelLight, skip.
                    if (lp == LUX_INVALID_PATH)
                        continue;
                    EmitGen(sample, path, lp, j, nE, lightPs.n[lp]);
                }
            }

            // A light list entry that cannot spawn!
            void CancelLight(uint32_t sample, uint32_t j)
            {
                tbl.SetLightDone(sample, j);
                CompleteWork(sample, 1u + (useT1 ? 1u : 0u));
            }

            void OnLightDone(uint32_t sample, uint32_t path, uint32_t j)
            {
                tbl.SetLightDone(sample, j);
                const uint32_t nL = lightPs.n[path];

                // Cam instance.
                if (useT1)
                {
                    const uint32_t cCnt = nL >= 1u ? nL : 0u;
                    ReserveItems(sample, cCnt);
                    for (uint32_t k = 0; k < cCnt; ++k)
                        PushItem(cam, {0u, path, sample, 0u, 1, int32_t(1 + k)});
                }

                if (tbl.eyeDone[sample])
                    EmitGen(sample, tbl.eyePath[sample], path, j,
                            eyePs.n[tbl.eyePath[sample]], nL);
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
#ifdef LUX2_INTEGRATOR_DEBUG
                if (tbl.pending[sample] < n)
                    fprintf(stderr,
                            "[WFSTAT] pending underflow s=%u have=%u n=%u\n",
                            sample, tbl.pending[sample], n);
#endif
                tbl.pending[sample] -= n;
                if (tbl.pending[sample] == 0u)
                {
                    retireQueue.push_back(sample);
                    return true;
                }
                return false;
            }

            // One lane finished.
            void CompleteItem(uint32_t sample)
            {
                if (queuedCount[sample] > 0u && --queuedCount[sample] == 0u)
                    --gateSamples;
                CompleteWork(sample, 1u);
            }

        private:
            // Expand one reserved token into `count` queued items.
            void ReserveItems(uint32_t sample, uint32_t count)
            {
                if (count == 0u)
                    CompleteWork(sample, 1u);
                else
                    tbl.pending[sample] += count - 1u;
            }

            void PushItem(ConnQueues &q, const ConnItem &it)
            {
                q.Push(it);
                if (queuedCount[it.sample]++ == 0u)
                    ++gateSamples;
            }

            // Enqueue the (nE-1)*nL general connections of one pair.
            void EmitGen(uint32_t sample, uint32_t ePath, uint32_t lPath,
                         uint32_t j, uint32_t nE, uint32_t nL)
            {
                const bool ok = nE >= 2u && nL >= 1u;
                const uint32_t cnt = ok ? (nE - 1u) * nL : 0u;
                WfStatPair(nE, nL);
                ReserveItems(sample, cnt);
                if (!ok)
                    return;
                const uint32_t sl = slotOf(sample, j);
                for (uint32_t t = 2; t <= nE; ++t)
                    for (uint32_t s = 1; s <= nL; ++s)
                        PushItem(gen, {ePath, lPath, sample, sl, int32_t(t),
                                       int32_t(s)});
            }

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
