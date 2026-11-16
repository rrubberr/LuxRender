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

// Run ERPT chains for a pool of seeds.

#ifndef LUX2_INTEGRATORS_ERPT_CHAIN_DRIVER_H
#define LUX2_INTEGRATORS_ERPT_CHAIN_DRIVER_H

#include "integrators/erpt/chain.h"
#include "integrators/erpt/deposit.h"
#include "integrators/erpt/caustic.h"
#include "integrators/erpt/lens.h"
#include "integrators/erpt/path_ops.h"
#include "integrators/erpt/seed_pool.h"
#include "integrators/erpt/sparse_accum.h"

#include <cmath>
#include <vector>

namespace lux2
{
    namespace erpt
    {
        class ChainDriver
        {
        public:
            static constexpr uint32_t kW = uint32_t(PACKET_WIDTH);

            void Drain(const WalkCtx &c, const SeedPool &pool,
                       const ChainParams &P, SparseAccum &acc, ChainStats &st)
            {
                if (pool.Empty())
                    return;
                EnsureStore();
                const LensMutator lens;
                const CausticMutator caustic;

                size_t nextSeed = 0;
                uint32_t chainsLeft = 0, chainIdx = 0;
                size_t curSeed = 0;
                auto nextChain = [&](size_t *si, uint32_t *cj) -> bool
                {
                    while (chainsLeft == 0u)
                    {
                        if (nextSeed >= pool.Count())
                            return false;
                        curSeed = nextSeed++;
                        const SeedHeader &h = pool.Header(curSeed);
                        Pcg32 r = KeyedPcg(h.seedKey, h.sidx, 0xFFFFFFFFu);
                        bool clamped = false;
                        chainsLeft = NumChains(h.e, P.m, P.ed, r.NextFloat(),
                                               P.maxChainsPerSeed, &clamped);
                        chainIdx = 0u;
                        ++st.seeds;
                        st.energyIn += h.e;
                        if (clamped)
                            ++st.chainClamped;
                    }
                    *si = curSeed;
                    *cj = chainIdx++;
                    --chainsLeft;
                    return true;
                };

                Lane L[kW];
                for (uint32_t i = 0; i < kW; ++i)
                {
                    L[i].cur = i;
                    L[i].scr = kW + i;
                }

                for (;;)
                {
                    bool any = false;
                    for (uint32_t i = 0; i < kW; ++i)
                    {
                        if (!L[i].active)
                        {
                            size_t si;
                            uint32_t cj;
                            while (nextChain(&si, &cj))
                            {
                                // A seed whose full-path f
                                // is <= 0 cannot start a chain.
                                if (Load(c, L[i], pool, si, cj))
                                    break;
                                const SeedHeader &h = pool.Header(si);
                                float X, Y, Z;
                                XYZOf1(h.wavelength, h.e, &X, &Y, &Z);
                                acc.Add(int32_t(std::floor(h.px)),
                                        int32_t(std::floor(h.py)), X, Y, Z, 0.f);
                                st.energyOut += h.e;
                                ++st.seedGateFail;
                                // Every remaining chain of this seed fails too.
                                chainsLeft = 0u;
                            }
                        }
                        any = any || L[i].active;
                    }
                    if (!any)
                        break;
                    Step(c, P, lens, caustic, L, acc, st);
                }
                st.chains += m_chains;
                m_chains = 0;
            }

        private:
            struct Lane
            {
                bool active = false;
                uint32_t cur = 0, scr = 0;
                uint32_t n = 0, steps = 0;
                float e = 0.f, wl = 650.f, px = 0.f, py = 0.f;
                Pcg32 rng;
                ConsecFilter cf;
            };

            wf::BlockArena m_arena;
            wf::PathStore m_ps;
            bool m_init = false;
            uint64_t m_chains = 0;

            void EnsureStore()
            {
                if (m_init)
                    return;
                m_ps.Init(&m_arena, 2u * kW);
                // MIS words must read as zero for chain slots.
                std::memset(m_arena.misWords.data(), 0,
                            m_arena.misWords.size() * sizeof(uint32_t));
                m_ps.n.assign(2u * kW, 0u);
                m_ps.lightIdx.assign(2u * kW, 0u);
                m_init = true;
            }

            // Load a seed into a lane.
            bool Load(const WalkCtx &c, Lane &l, const SeedPool &pool, size_t si,
                      uint32_t cj)
            {
                const SeedHeader &h = pool.Header(si);
                l.active = true;
                ++m_chains;
                l.n = h.n;
                l.e = h.e;
                l.wl = h.wavelength;
                l.px = h.px;
                l.py = h.py;
                l.steps = 0;
                l.rng = KeyedPcg(h.seedKey, h.sidx, cj);
                l.cf.Reset(Pixel{int32_t(std::floor(h.px)), int32_t(std::floor(h.py))});
                const size_t words = size_t(h.n) * wf::GEOM_WORDS;
                for (uint32_t id : {l.cur, l.scr})
                {
                    std::memcpy(&m_arena.geomWords[size_t(id) * wf::PATH_STRIDE *
                                                   wf::GEOM_WORDS],
                                pool.Verts(si), words * sizeof(uint32_t));
                    m_ps.n[id] = h.n;
                    m_ps.lightIdx[id] = h.lightIdx;
                }

                // Chain start fx gate.
                SpectrumWavelengthsP sw;
                sw.FromWavelength(FloatP(h.wavelength));
                MaskP ok(false);
                const SWCSpectrumP fx =
                    EvalCanonF(c, m_ps, UInt32P(l.cur), Int32P(int(h.n)), sw,
                               /*traceVisibility=*/false, MaskP(true), &ok);
                if (!enoki::any(enoki::hmax(fx) > FloatP(0.f)))
                {
                    l.active = false;
                    --m_chains;
                    return false;
                }
                return true;
            }

            void Step(const WalkCtx &c, const ChainParams &P, const Mutator &lens,
                      const Mutator &caustic, Lane *L, SparseAccum &acc,
                      ChainStats &st)
            {
                alignas(64) uint32_t curA[kW], scrA[kW], nA[kW];
                alignas(64) float wlA[kW], act[kW], pickLens[kW];
                MutRands R;
                alignas(64) float ru[2][kW];
                for (uint32_t i = 0; i < kW; ++i)
                {
                    Lane &l = L[i];
                    curA[i] = l.cur;
                    scrA[i] = l.scr;
                    nA[i] = l.active ? l.n : 3u;
                    wlA[i] = l.wl;
                    act[i] = l.active ? 1.f : 0.f;
                    for (int k = 0; k < 2; ++k)
                        ru[k][i] = l.active ? l.rng.NextFloat() : 0.f;
                    pickLens[i] = (l.active && l.rng.NextFloat() < 0.5f) ? 1.f : 0.f;
                }
                for (int k = 0; k < 2; ++k)
                    R.u[k] = enoki::load_unaligned<FloatP>(ru[k]);
                const UInt32P cur = enoki::load_unaligned<UInt32P>(curA);
                const UInt32P scr = enoki::load_unaligned<UInt32P>(scrA);
                const Int32P n = Int32P(enoki::load_unaligned<UInt32P>(nA));
                const MaskP active = enoki::load_unaligned<FloatP>(act) > FloatP(0.f);
                const MaskP isLens =
                    active && (enoki::load_unaligned<FloatP>(pickLens) > FloatP(0.f));
                const MaskP isCaus = active && !isLens;
                SpectrumWavelengthsP sw;
                sw.FromWavelength(enoki::load_unaligned<FloatP>(wlA));

                MutationResult a = lens.Propose(c, m_ps, cur, scr, n, sw, R, isLens);
                MutationResult b =
                    caustic.Propose(c, m_ps, cur, scr, n, sw, R, isCaus);
                const MaskP ok = (isLens && a.ok) || (isCaus && b.ok);
                const Int32P spanLo = select(isLens, a.spanLo, b.spanLo);
                const Int32P spanHi = select(isLens, a.spanHi, b.spanHi);
                const Int32P winLo = select(isLens, a.winLo, b.winLo);
                const Int32P winHi = select(isLens, a.winHi, b.winHi);
                const FloatP tRatio = select(isLens, a.tRatio, b.tRatio);

                MaskP okY(false), okX(false), pxOk(false);
                const SWCSpectrumP Fy = EvalCanonWindow(
                    c, m_ps, scr, n, winLo, winHi, sw, true, ok, &okY);
                const SWCSpectrumP Fx = EvalCanonWindow(
                    c, m_ps, cur, n, winLo, winHi, sw, false, ok, &okX);
                FloatP qx, qy, qWe;
                pxOk = PathPixel(c, m_ps, scr, &qx, &qy, &qWe, ok && okY);

                alignas(64) float fxA[kW], fyA[kW], tA[kW], qxA[kW], qyA[kW],
                    vA[kW];
                alignas(64) int32_t loA[kW], hiA[kW];
                enoki::store_unaligned(fxA, Fx);
                enoki::store_unaligned(fyA, Fy);
                enoki::store_unaligned(tA, tRatio);
                enoki::store_unaligned(qxA, qx);
                enoki::store_unaligned(qyA, qy);
                enoki::store_unaligned(vA, select(ok && okY && pxOk, FloatP(1.f),
                                                  FloatP(0.f)));
                enoki::store_unaligned(loA, spanLo);
                enoki::store_unaligned(hiA, spanHi);

                // Deposits gathered here.
                struct Dep
                {
                    int32_t x, y;
                    float amt, wl;
                };
                // Max 2 deposits per active lane per step.
                Dep depsStack[2 * kW];
                size_t ndep = 0;

                for (uint32_t i = 0; i < kW; ++i)
                {
                    Lane &l = L[i];
                    if (!l.active)
                        continue;
                    ++st.steps;
                    StepIn in;
                    in.valid = vA[i] > 0.f;
                    in.x = Pixel{int32_t(std::floor(l.px)), int32_t(std::floor(l.py))};
                    in.y = Pixel{int32_t(std::floor(qxA[i])), int32_t(std::floor(qyA[i]))};
                    in.a = in.valid ? AcceptProb(fxA[i], fyA[i], tA[i]) : 0.f;
                    in.u = l.rng.NextFloat();
                    ++st.proposed;
                    st.valid += in.valid ? 1u : 0u;
                    if (in.valid && !(fxA[i] > 0.f))
                        ++st.fxZero;
                    const StepOut so = StepChain(P, l.cf, in);
                    for (int k = 0; k < so.nDep; ++k)
                    {
                        depsStack[ndep++] = {so.depPx[k].x, so.depPx[k].y,
                                             so.depAmount[k], l.wl};
                        st.energyOut += so.depAmount[k];
                    }
                    if (so.proposedInto)
                        acc.Add(so.proposedPx.x, so.proposedPx.y, 0.f, 0.f, 0.f, 1.f);
                    if (so.accept)
                    {
                        ++st.accepted;
                        std::swap(l.cur, l.scr);
                        l.px = qxA[i];
                        l.py = qyA[i];
                    }
                    // scratch <- current over the written span.
                    CopySpan(m_ps, l.scr, m_ps, l.cur, uint32_t(loA[i]),
                             uint32_t(hiA[i]));
                    if (++l.steps >= P.m)
                        l.active = false;
                }

                for (size_t base = 0; base < ndep; base += kW)
                {
                    alignas(64) float wl[kW], amt[kW], X[kW], Y[kW], Z[kW];
                    const size_t cnt = std::min<size_t>(kW, ndep - base);
                    for (uint32_t i = 0; i < kW; ++i)
                    {
                        wl[i] = i < cnt ? depsStack[base + i].wl : 650.f;
                        amt[i] = i < cnt ? depsStack[base + i].amt : 0.f;
                    }
                    XYZOfBatch(wl, amt, X, Y, Z);
                    for (size_t i = 0; i < cnt; ++i)
                        acc.Add(depsStack[base + i].x, depsStack[base + i].y,
                                X[i], Y[i], Z[i], 0.f);
                }
            }
        };

    } // namespace erpt
} // namespace lux2

#endif // LUX2_INTEGRATORS_ERPT_CHAIN_DRIVER_H
