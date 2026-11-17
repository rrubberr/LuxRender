/***************************************************************************
 *   Copyright (C) 1998-2026 by authors (see AUTHORS.txt)                  *
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

#include "core/renderinghints.h"
#include "core/light.h"
#include "core/scene.h"
#include "core/error.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace lux2
{
    namespace
    {

        // Weight per light for the requested kind.
        float LightWeight(const Light *l, int kind)
        {
            const float imp = l->Importance();
            switch (kind)
            {
            case 0:
                return std::max(imp, 0.f); // importance
            case 2:
                return std::max(imp * l->Power(), 0.f); // powerimp
            case 3:
            { // logpowerimp
                // Clamp at 0 so the CDF is a non-negative density.
                const float w = imp * l->Power();
                return std::max(std::log(w > 0.f ? w : 1e-20f), 0.f);
            }
            }
            return imp;
        }

        // Inclusive cumulative of length n+1 cdf[0]==0, cdf[n]==1.
        void BuildCdf(const std::vector<float> &w, std::vector<float> *cdf)
        {
            const size_t n = w.size();
            cdf->assign(n + 1, 0.f);
            double sum = 0.0;
            for (size_t i = 0; i < n; ++i)
                sum += double(w[i]);
            double acc = 0.0;
            for (size_t i = 0; i < n; ++i)
            {
                acc += double(w[i]);
                (*cdf)[i + 1] = float(acc / sum);
            }
            cdf->back() = 1.f;
        }

        // Strategies.
        struct StrategySpec
        {
            int weightKind = -1;
            bool isAll = false;
        };

        StrategySpec ResolveStrategy(const std::string &name, size_t n)
        {
            if (name == "one")
                return {-1, false};
            if (name == "all")
                return {-1, true};
            if (name == "importance")
                return {0, false};
            if (name == "powerimp")
                return {2, false};
            if (name == "logpowerimp")
                return {3, false};
            if (name == "allpowerimp")
                return {2, true};
            if (name == "auto")
                return {-1, n <= 5u};
            if (name == "autopowerimp")
                return {2, n <= 5u};
            // Warn once per unknown.
            static std::unordered_set<std::string> warned;
            if (warned.insert(name).second)
                LOG(LUX_WARNING, LUX_BADTOKEN)
                    << "lightstrategy '" << name << "' unknown. Using \"one\".";
            return {-1, false};
        }

        // Remap All where unsupported (allowAll == false).
        static bool NormalizeLightStrategyNameImpl(const std::string &in,
                                                   std::string *out)
        {
            if (in == "all" || in == "auto")
            {
                *out = "one";
                return true;
            }
            if (in == "allpowerimp" || in == "autopowerimp")
            {
                *out = "powerimp";
                return true;
            }
            *out = in;
            return false;
        }

        // A delta light has no surface. Infinite lights report TotalArea()==0
        // from the base class but are not delta.
        bool IsDelta(const Light *l) { return !l->IsInfinite() && l->TotalArea() <= 0.f; }

    } // namespace

    // So integrators can link to it.
    UInt32P SearchSortedCDF(const std::vector<float> &cdf, const FloatP &uIn,
                            const MaskP &active)
    {
        // Clamp u into [0, 1) so u >= 1 cannot walk past the last bucket.
        const FloatP u = enoki::min(uIn, FloatP(1.f - 1e-6f));
        const UInt32P nb = UInt32P(uint32_t(cdf.size() - 1u));
        UInt32P lo(0u), hi(nb + UInt32P(1u));
        for (;;)
        {
            const MaskP live = active && (lo < hi);
            if (!enoki::any(live))
                break;
            const UInt32P mid = (lo + hi) >> 1u;
            const FloatP cdfMid = enoki::gather<FloatP>(cdf.data(), mid, live);
            const MaskP advance = live && (cdfMid <= u);
            lo = enoki::select(advance, mid + UInt32P(1u), lo);
            hi = enoki::select(live && !advance, mid, hi);
        }
        UInt32P idx = lo - UInt32P(1u); // first index with cdf[lo] > u, minus 1
        idx = enoki::min(idx, nb - UInt32P(1u));
        return enoki::select(active, idx, UInt32P(0u));
    }

    bool NormalizeLightStrategyName(const std::string &in, std::string *out,
                                    bool allowAll)
    {
        if (allowAll)
        {
            *out = in;
            return false;
        }
        static std::once_flag once;
        const bool changed = NormalizeLightStrategyNameImpl(in, out);
        if (changed)
        {
            std::call_once(once, []
                           { LOG(LUX_WARNING, LUX_SYSTEM)
                                 << "lightstrategy: \"all\" mode not yet supported here; "
                                    "downgrading to \"one\"."; });
        }
        return changed;
    }

    // Uniform/Cdf selection of one light for NEE.
    LightPick SampleLight(const LightsSamplingStrategy &ls, const FloatP &uLight,
                          const MaskP &active)
    {
        const uint32_t n = uint32_t(ls.pSel.size());
        const UInt32P idx =
            (ls.kind == LightsSamplingStrategy::StrategyKind::Cdf)
                ? SearchSortedCDF(ls.cdf, uLight, active)
                : enoki::min(
                      UInt32P(uint32_t(n - 1u)),
                      enoki::floor2int<UInt32P>(
                          uLight * FloatP(float(n))));
        LightPick pick;
        pick.idx = idx;
        pick.pSel = enoki::gather<FloatP>(ls.pSel.data(), idx, active);
        return pick;
    }

    LightsSamplingStrategy CreateLightsSamplingStrategy(
        const std::string &name,
        const std::vector<const Light *> &group,
        int groupId,
        const Scene &scene,
        LightRole role,
        bool allowAll)
    {
        LightsSamplingStrategy ls;
        const size_t n = group.size();
        if (n == 0)
            return ls;

        std::string normName;
        NormalizeLightStrategyName(name, &normName, allowAll);
        const StrategySpec spec = ResolveStrategy(normName, n);
        const int weightKind = spec.weightKind;
        const bool isAll = spec.isAll;
        const bool lightPath = role == LightRole::LightPath;

        // `degenerate` is set only when the weights cannot form a CDF.
        ls.pSel.assign(n, 1.f / float(n));
        bool degenerate = false;
        if (weightKind >= 0)
        {
            std::vector<float> w(n);
            double sum = 0.0;
            for (size_t i = 0; i < n; ++i)
            {
                w[i] = LightWeight(group[i], weightKind);
                sum += double(w[i]);
            }
            if (!(sum > 0.0) || !std::isfinite(sum))
            {
                degenerate = true;
                LOG(LUX_WARNING, LUX_SYSTEM)
                    << "lightstrategy weights are zero/non-finite; using uniform.";
            }
            else
            {
                for (size_t i = 0; i < n; ++i)
                    ls.pSel[i] = float(double(w[i]) / sum);
            }
        }

        // CDF for single light weighted strategies.
        if (weightKind >= 0 && !degenerate && !isAll)
        {
            BuildCdf(ls.pSel, &ls.cdf);
            ls.kind = LightsSamplingStrategy::StrategyKind::Cdf;
        }

        // Task list and pSel multiplicities.
        if (isAll)
        {
            if (weightKind < 0 || degenerate)
            {
                for (uint32_t k = 0; k < n; ++k)
                    ls.task.push_back(k); // all/auto each light once
            }
            else
            {
                // allpowerimp/autopowerimp N evenly spaced u through the CDF.
                std::vector<float> cdf;
                BuildCdf(ls.pSel, &cdf);
                for (uint32_t s = 0; s < n; ++s)
                {
                    const float u = (float(s) + 0.5f) / float(n);
                    size_t k = size_t(std::upper_bound(cdf.begin(), cdf.end(), u) - cdf.begin()) - 1u;
                    ls.task.push_back(uint32_t(std::min(k, n - 1)));
                }
            }

            std::vector<float> mult(n, 0.f);
            for (uint32_t k : ls.task)
                mult[k] += 1.f;
            // Every delta light gets at least one slot on the direct side.
            if (!lightPath)
            {
                for (uint32_t k = 0; k < n; ++k)
                {
                    if (mult[k] == 0.f && IsDelta(group[k]))
                    {
                        mult[k] = 1.f;
                        ls.task.push_back(k);
                    }
                }
            }
            ls.pSel = std::move(mult); // pSel == n_k
            ls.kind = LightsSamplingStrategy::StrategyKind::All;
        }

        // Zero pSel check.
        for (uint32_t k = 0; k < n; ++k)
        {
            if (ls.pSel[k] != 0.f)
                continue;
            if (lightPath && IsDelta(group[k]))
                continue; // delta lights cannot start a light subpath
            LOG(LUX_WARNING, LUX_SYSTEM)
                << "lightstrategy: light " << k
                << (IsDelta(group[k])
                        ? " (delta) has zero sampling weight and will not appear."
                    : lightPath
                        ? " has zero sampling weight; it never starts a"
                          " light subpath and is reachable only through BSDF"
                          " hits and nee."
                        : " has zero sampling weight; it is reachable only"
                          " through BSDF hits and will be absent with"
                          " lightmode=nee.");
        }

        // By area light id, pointer map.
        std::unordered_map<const Light *, uint32_t> kOf;
        kOf.reserve(n);
        for (uint32_t k = 0; k < n; ++k)
            kOf[group[k]] = k;
        const int nArea = scene.AreaLightCount();
        ls.areaPSel.assign(nArea > 0 ? size_t(nArea) : 0u, 0.f);
        for (int a = 0; a < nArea; ++a)
        {
            if (scene.AreaLightGroup(a) != groupId)
                continue; // other group
            const Light *al = scene.GetAreaLight(a);
            if (!al)
                continue;
            auto it = kOf.find(al);
            assert(it != kOf.end()); // in group
            if (it != kOf.end())
                ls.areaPSel[a] = ls.pSel[it->second];
        }
        return ls;
    }

} // namespace lux2
