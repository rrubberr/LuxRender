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

// Wavefront vector MIS.

#ifndef LUX2_INTEGRATORS_WF_MIS_H
#define LUX2_INTEGRATORS_WF_MIS_H

#include "integrators/debug/integrator_debug.h"
#include "integrators/wf/storage.h"
#include "integrators/wf/types.h"
#include "integrators/lane/math.h"

#include "core/math/math.h" // RR_START

#include <enoki/array.h>

namespace lux2
{
    namespace wf
    {
        // Read one MIS float word of record `rec`.
        inline FloatP MisF(const PathStore &ps, const UInt32P &rec, uint32_t w,
                           const MaskP &m)
        {
            return reinterpret_array<FloatP>(
                LoadWord(ps.arena->misWords.data(), RecWord<MIS_WORDS>(rec, w), m));
        }

        // A view exposes five MIS fields for WeightPathV, indexed by
        // vector vertex index and validity mask.
        struct PoolView
        {
            const PathStore *ps = nullptr;
            UInt32P path; // path slot per lane

            FloatP dAW(const Int32P &i, const MaskP &m) const
            {
                return MisF(*ps, RecIdx(*ps, path, i, m), kWord_dAW, m);
            }
            FloatP dARW(const Int32P &i, const MaskP &m) const
            {
                return MisF(*ps, RecIdx(*ps, path, i, m), kWord_dARW, m);
            }
            FloatP rr(const Int32P &i, const MaskP &m) const
            {
                return MisF(*ps, RecIdx(*ps, path, i, m), kWord_rr, m);
            }
            FloatP rrR(const Int32P &i, const MaskP &m) const
            {
                return MisF(*ps, RecIdx(*ps, path, i, m), kWord_rrR, m);
            }
            MaskP spec(const Int32P &i, const MaskP &m) const
            {
                const UInt32P bits = LoadWord(
                    ps->arena->misWords.data(),
                    RecWord<MIS_WORDS>(RecIdx(*ps, path, i, m), kWord_bits), m);
                return neq(bits & UInt32P(1u), UInt32P(0u));
            }
        };

        struct Virtual1
        {
            FloatP aF; // dAW(0) every other field neutral
            FloatP dAW(const Int32P &i, const MaskP &) const
            {
                return select(eq(i, Int32P(0)), aF, FloatP(0.f));
            }
            FloatP dARW(const Int32P &, const MaskP &) const
            {
                return FloatP(0.f);
            }
            FloatP rr(const Int32P &, const MaskP &) const { return FloatP(1.f); }
            FloatP rrR(const Int32P &, const MaskP &) const { return FloatP(1.f); }
            MaskP spec(const Int32P &, const MaskP &) const
            {
                return MaskP(false);
            }
        };

        struct NullView
        {
            FloatP dAW(const Int32P &, const MaskP &) const
            {
                return FloatP(0.f);
            }
            FloatP dARW(const Int32P &, const MaskP &) const
            {
                return FloatP(0.f);
            }
            FloatP rr(const Int32P &, const MaskP &) const { return FloatP(1.f); }
            FloatP rrR(const Int32P &, const MaskP &) const { return FloatP(1.f); }
            MaskP spec(const Int32P &, const MaskP &) const
            {
                return MaskP(false);
            }
        };

        template <class EyeV, class LightV>
        inline FloatP WeightPathV(const WalkCtx &ctx, const EyeV &eye,
                                  const Int32P &nE, const LightV &light,
                                  const Int32P &nL, const Transient &tr,
                                  const MaskP &act)
        {
            if (ctx.cfg.noMis)
                return FloatP(1.f); // scalar early out

            const float k = ctx.cfg.misPower;
            const Int32P one(1), zero(0);

            // Transient overrides apply at idx == nE-1 / nL-1 (and nE-2 / nL-2).
            auto eAF = [&](const Int32P &i, const MaskP &m)
            {
                return select(eq(i, nE - one), tr.eAF_last, eye.dARW(i, m));
            };
            auto eAR = [&](const Int32P &i, const MaskP &m)
            {
                return select(eq(i, nE - one), tr.eAR_last,
                              select(eq(i, nE - (one + one)), tr.eAR_prev,
                                     eye.dAW(i, m)));
            };
            auto eRR = [&](const Int32P &i, const MaskP &m)
            {
                return select(eq(i, nE - one), tr.eRR_last, eye.rr(i, m));
            };
            auto eRRR = [&](const Int32P &i, const MaskP &m)
            {
                return select(eq(i, nE - one), tr.eRRR_last, eye.rrR(i, m));
            };
            auto eSpec = [&](const Int32P &i, const MaskP &m)
            {
                return select(eq(i, nE - one), MaskP(false), eye.spec(i, m));
            };
            auto lAF = [&](const Int32P &i, const MaskP &m)
            {
                return light.dAW(i, m);
            };
            auto lAR = [&](const Int32P &i, const MaskP &m)
            {
                return select(eq(i, nL - one), tr.lAR_last,
                              select(eq(i, nL - (one + one)), tr.lAR_prev,
                                     light.dARW(i, m)));
            };
            auto lRR = [&](const Int32P &i, const MaskP &m)
            {
                return select(eq(i, nL - one), tr.lRR_last, light.rr(i, m));
            };
            auto lRRR = [&](const Int32P &i, const MaskP &m)
            {
                return select(eq(i, nL - one), tr.lRRR_last, light.rrR(i, m));
            };
            auto lSpec = [&](const Int32P &i, const MaskP &m)
            {
                return select(eq(i, nL - one), MaskP(false), light.spec(i, m));
            };

            const MaskP nl1 = eq(nL, one);
            const MaskP nl0 = eq(nL, zero);
            const MaskP hasL = nL >= one;
            const FloatP aF0 =
                select(hasL, light.dAW(zero, act && hasL), FloatP(0.f));
            const MaskP isLD = MaskP(tr.isLightDirect);
            const FloatP pBase =
                select(nl1 && isLD, divp(aF0, tr.pdfLightDirect), FloatP(1.f));
            FloatP weight(1.f), p = pBase;

            // nL == 1 direct origin term.
            {
                const MaskP m = aF0 > FloatP(0.f);
                if (ctx.lightDepth > 0)
                    weight = select(nl1 && isLD && m, weight + pw(pBase, k),
                                    weight);
                const FloatP pDirect = divp(tr.pdfLightDirect, aF0);
                weight = select(nl1 && (!isLD) && m, weight + pw(pDirect, k),
                                weight);
            }

            // nL == 0 when eye path hit a light (OnPassThrough always has nE >= 2).
            {
                const MaskP cond = act && nl0;
                const MaskP wide = cond && (nE >= (one + one));
                FloatP pDirect = divp(tr.pdfLightDirect, eAF(nE - one, cond));
                const MaskP rrOn = cond && (nE > Int32P(RR_START + 1));
                pDirect = select(rrOn, divp(pDirect, eye.rrR(nE - (one + one), rrOn)),
                                 pDirect);
                const MaskP m = !eSpec(nE - (one + one), wide);
                weight = select(cond && m, weight + pw(pDirect, k), weight);
            }

            // Extend the light path toward the eye path. The eye path's first
            // vertex is a surface hit.
            constexpr int minEye = 1;
            const Int32P nLightExt =
                enoki::min(nE - Int32P(minEye),
                           Int32P(ctx.lightDepth) -
                               enoki::min(Int32P(ctx.lightDepth), nL));
            const int iMax = enoki::hmax(select(act, nLightExt, zero));
            MaskP ok = act;
            for (int i = 1; i <= iMax; ++i)
            {
                const MaskP live = act && (nLightExt >= Int32P(i));
                const Int32P idx = nE - Int32P(i); // >= minEye >= 1 on live
                ok = ok && (eAR(idx, live) > FloatP(0.f)) &&
                     (eAF(idx, live) > FloatP(0.f));
                FloatP pn = p * divp(eAR(idx, live), eAF(idx, live));
                const MaskP rrDiv = live && (idx > Int32P(RR_START));
                pn = select(rrDiv, divp(pn, eye.rrR(idx - one, rrDiv)), pn);
                const MaskP rrOn =
                    live && ((nL + Int32P(i)) > Int32P(RR_START + 1));
                pn = select(rrOn,
                            pn * (i == 1 ? lRR(nL - one, rrOn)
                                         : eRR(idx + one, rrOn)),
                            pn);
                p = select(live, pn, p);
                const MaskP prevValid = live && (idx > zero);
                const MaskP specPrev =
                    select(prevValid, !eSpec(idx - one, prevValid), MaskP(true));
                const MaskP specOk = !eSpec(idx, live) && specPrev;
                weight = select(live && ok && specOk, weight + pw(p, k), weight);
            }

            // Extend the eye path toward the light path.
            p = pBase;
            const Int32P nEyeExt =
                enoki::min(nL,
                           Int32P(ctx.eyeDepth) -
                               enoki::min(Int32P(ctx.eyeDepth), nE));
            const int jMax = enoki::hmax(select(act, nEyeExt, zero));
            ok = act;
            for (int i = 1; i <= jMax; ++i)
            {
                const MaskP live = act && (nEyeExt >= Int32P(i));
                const Int32P idx = nL - Int32P(i);
                ok = ok && (lAR(idx, live) > FloatP(0.f)) &&
                     (lAF(idx, live) > FloatP(0.f));
                FloatP pn = p * divp(lAR(idx, live), lAF(idx, live));
                const MaskP rrDiv = live && (idx > Int32P(RR_START));
                pn = select(rrDiv, divp(pn, lRR(idx - one, rrDiv)), pn);
                const MaskP rrOn =
                    live && ((nE + Int32P(i)) > Int32P(RR_START + 1));
                pn = select(rrOn,
                            pn * (i == 1 ? eRRR(nE - one, rrOn)
                                         : lRRR(idx + one, rrOn)),
                            pn);
                p = select(live, pn, p);
                const MaskP prevValid = live && (idx > zero);
                const MaskP specPrev =
                    select(prevValid, !lSpec(idx - one, prevValid), MaskP(true));
                const MaskP specOk = !lSpec(idx, live) && specPrev;
                weight = select(live && ok && specOk, weight + pw(p, k), weight);
                // Direct path's second vertex must be non-specular (i == nL - 1).
                const MaskP dm = ok && live && eq(Int32P(i), nL - one) &&
                                 !lSpec(one, live) && (aF0 > FloatP(0.f));
                const FloatP pDirect = p * divp(tr.pdfLightDirect, aF0);
                weight = select(dm, weight + pw(pDirect, k), weight);
            }
            WfStatMis(iMax, jMax,
                      enoki::hsum(select(act, nLightExt, zero)),
                      enoki::hsum(select(act, nEyeExt, zero)),
                      enoki::count(act));
            return weight;
        }

    } // namespace wf
} // namespace lux2

#endif // LUX2_INTEGRATORS_WF_MIS_H
