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

// Seed capture for ERPT to turn bidirectional connections into
// a chain seed or an immediate deposit.

#ifndef LUX2_INTEGRATORS_ERPT_SEED_CAPTURE_H
#define LUX2_INTEGRATORS_ERPT_SEED_CAPTURE_H

#include "integrators/erpt/canon_path.h"
#include "integrators/erpt/chain.h"
#include "integrators/erpt/chain_driver.h"
#include "integrators/erpt/seed_pool.h"
#include "integrators/erpt/smooth.h"
#include "integrators/erpt/sparse_accum.h"
#include "integrators/wf/connectors.h"
#include "integrators/wf/seed_sink.h"

#include <atomic>
#include <mutex>

namespace lux2
{
    namespace erpt
    {
        // State shared by all workers.
        struct ErptShared
        {
            // Config.
            ChainParams params;
            double calSamples = 20000.0;
            bool autoEd = true;
            size_t poolBytes = size_t(8) << 20;
            bool smoothing = false;
            int smoothRadius = 4;

            // Calibration.
            std::mutex mu;
            double calEnergy = 0.0;
            double calCount = 0.0;
            std::atomic<bool> frozen{false};
            std::atomic<float> ed{0.f};

            // Proposal tally for the smoothing filter.
            FilterGrid proposals;
            ChainStats stats;

            void Freeze(float v)
            {
                ed.store(v);
                frozen.store(true);
            }
            void AddCalibration(double energy, double nSamples)
            {
                if (frozen.load())
                    return;
                std::lock_guard<std::mutex> lk(mu);
                if (frozen.load())
                    return;
                calEnergy += energy;
                calCount += nSamples;
                if (calCount >= calSamples && calEnergy > 0.0)
                    Freeze(float((calEnergy / calCount) / double(params.m)));
            }
        };

        // ERPT worker state.
        class ErptSeedSink final : public wf::SeedSink
        {
        public:
            ErptSeedSink(ErptShared &sh) : m_sh(sh), pool(32u, sh.poolBytes) {}

            SeedPool pool;
            SparseAccum acc;
            ChainDriver driver;
            ChainStats stats;
            double calEnergy = 0.0; // this pixel's calibration energy

            void Emit(const WalkCtx &c, const wf::SampleTable &tbl,
                      const wf::SeedRec &rec) override
            {
                using namespace wf;
                alignas(64) float eA[PACKET_WIDTH], pxA[PACKET_WIDTH],
                    pyA[PACKET_WIDTH];
                alignas(64) uint32_t smp[PACKET_WIDTH], eP[PACKET_WIDTH],
                    lP[PACKET_WIDTH], vL[PACKET_WIDTH], vA[PACKET_WIDTH];
                alignas(64) int32_t nE[PACKET_WIDTH], nL[PACKET_WIDTH];
                alignas(64) float vP[3][PACKET_WIDTH], vN[3][PACKET_WIDTH],
                    vG[3][PACKET_WIDTH], contA[PACKET_WIDTH];
                enoki::store_unaligned(eA, rec.e);
                enoki::store_unaligned(pxA, rec.px);
                enoki::store_unaligned(pyA, rec.py);
                enoki::store_unaligned(smp, rec.sample);
                enoki::store_unaligned(eP, rec.ePath);
                enoki::store_unaligned(lP, rec.lPath);
                enoki::store_unaligned(vL, rec.vLightIdx);
                enoki::store_unaligned(vA, rec.vAreaId);
                enoki::store_unaligned(nE, rec.nE);
                enoki::store_unaligned(nL, rec.nL);
                enoki::store_unaligned(contA, select(rec.cont, FloatP(1.f), FloatP(0.f)));
                for (int k = 0; k < 3; ++k)
                {
                    enoki::store_unaligned(vP[k], rec.vP[k]);
                    enoki::store_unaligned(vN[k], rec.vN[k]);
                    enoki::store_unaligned(vG[k], rec.vNg[k]);
                }

                const bool camKind = rec.kind == SeedKind::Cam ||
                                     rec.kind == SeedKind::CamNee;
                const bool seeding = m_sh.frozen.load(std::memory_order_acquire);
                const float ed = m_sh.ed.load();

                for (uint32_t i = 0; i < PACKET_WIDTH; ++i)
                {
                    if (contA[i] <= 0.f)
                        continue;
                    const float e = eA[i];
                    if (!(e > 0.f) || !std::isfinite(e))
                        continue;
                    const uint32_t s = smp[i];
                    const float px = camKind ? pxA[i] : tbl.x[s];
                    const float py = camKind ? pyA[i] : tbl.y[s];
                    const float wl = tbl.w[s];

                    if (!seeding)
                    {
                        calEnergy += e;
                        Direct(px, py, e, wl);
                        continue;
                    }

                    SeedLane L;
                    L.kind = rec.kind;
                    L.eyePs = rec.eyePs;
                    L.ePath = eP[i];
                    L.nE = uint32_t(nE[i]);
                    L.lightPs = rec.lightPs;
                    L.lPath = lP[i];
                    L.nL = uint32_t(nL[i]);
                    for (int k = 0; k < 3; ++k)
                    {
                        L.vP[k] = vP[k][i];
                        L.vN[k] = vN[k][i];
                        L.vNg[k] = vG[k][i];
                    }
                    L.vLightIdx = vL[i];
                    L.vAreaId = vA[i];

                    CanonInfo info;
                    if (!BuildCanonicalSeed(c, L, m_scratch, &info))
                    {
                        Direct(px, py, e, wl); // class C
                        continue;
                    }
                    SeedHeader h;
                    h.e = e;
                    h.wavelength = wl;
                    h.seedKey = tbl.seed[s];
                    // Several seeds can share one sample slot so
                    // make the key unique per path.
                    h.sidx = HashKey(tbl.sidx[s], uint32_t(rec.kind), L.nE, L.nL,
                                     L.lightPs ? L.lightPs->lightSlot[L.lPath] : 0u);
                    h.n = info.n;
                    h.lightIdx = info.lightIdx;
                    h.px = px;
                    h.py = py;
                    uint32_t *dst = pool.Begin(h);
                    std::memcpy(dst, m_scratch.data(),
                                m_scratch.size() * sizeof(uint32_t));
                    (void)ed;
                }
            }

            // Run all pooled chains into `acc`.
            void Drain(const WalkCtx &c)
            {
                ChainParams P = m_sh.params;
                P.ed = m_sh.ed.load();
                driver.Drain(c, pool, P, acc, stats);
                pool.Clear();
            }

        private:
            ErptShared &m_sh;
            std::vector<uint32_t> m_scratch;

            static uint32_t HashKey(uint32_t a, uint32_t b, uint32_t c2,
                                    uint32_t d, uint32_t e)
            {
                uint64_t x = (uint64_t(a) << 32) | b;
                x = SplitMix64(x ^ ((uint64_t(c2) << 40) | (uint64_t(d) << 20) | e));
                return uint32_t(x ^ (x >> 32));
            }

            void Direct(float px, float py, float e, float wl)
            {
                alignas(64) float W[PACKET_WIDTH], A[PACKET_WIDTH], X[PACKET_WIDTH],
                    Y[PACKET_WIDTH], Z[PACKET_WIDTH];
                for (uint32_t i = 0; i < PACKET_WIDTH; ++i)
                {
                    W[i] = wl;
                    A[i] = i == 0 ? e : 0.f;
                }
                XYZOfBatch(W, A, X, Y, Z);
                acc.Add(int32_t(std::floor(px)), int32_t(std::floor(py)), X[0],
                        Y[0], Z[0], 0.f);
                stats.energyOut += e;
            }
        };

    } // namespace erpt
} // namespace lux2

#endif // LUX2_INTEGRATORS_ERPT_SEED_CAPTURE_H
