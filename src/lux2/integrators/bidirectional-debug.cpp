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

// Debug instrumentation for the bidirectional integrator.

#ifdef LUX2_INTEGRATOR_DEBUG

#include "integrators/integrator_debug.h"

#include "core/bsdf_call.h"
#include "core/error.h"
#include "core/math.h"

#include <enoki/array.h>

#include <algorithm>
#include <limits>
#include <mutex>
#include <vector>

namespace lux2
{
    void BidirDiagStore::Init(int eyeDepth, int lightDepth)
    {
        ghost.assign(eyeDepth + 2,
                     std::vector<GhostCell>(lightDepth + 2));
    }

    // Caller holds mutex.
    void BidirDiagStore::InsertFirefly(const Firefly &f)
    {
        if (top.size() < kTopN)
        {
            top.push_back(f);
            std::push_heap(top.begin(), top.end(),
                           [](const Firefly &a, const Firefly &b)
                           { return a.lcw > b.lcw; });
        }
        else if (f.lcw > top.front().lcw)
        {
            std::pop_heap(top.begin(), top.end(),
                          [](const Firefly &a, const Firefly &b)
                          { return a.lcw > b.lcw; });
            top.back() = f;
            std::push_heap(top.begin(), top.end(),
                           [](const Firefly &a, const Firefly &b)
                           { return a.lcw > b.lcw; });
        }
    }

    void BidirDiagLocal::Init(int eyeDepth, int lightDepth)
    {
        ghost.assign(eyeDepth + 2,
                     std::vector<GhostCell>(lightDepth + 2));
    }

    // Keep only the kTopN largest by lcw.
    void BidirDiagLocal::TrimFireflies()
    {
        const size_t n = BidirDiagStore::kTopN;
        if (fireflies.size() <= n)
            return;
        std::nth_element(fireflies.begin(), fireflies.begin() + n,
                         fireflies.end(),
                         [](const BidirDiagStore::Firefly &a,
                            const BidirDiagStore::Firefly &b)
                         { return a.lcw > b.lcw; });
        fireflies.resize(n);
    }

    // For a uniformly sampled area light, converting the solid angle Pdf_L
    // at the receiver to the emitter's area measure must recover the
    // selection pdf.
    void BidirCheckAreaDensity(const char *what, const FloatP &areaPdf,
                               const FloatP &area, const FloatP &selPdf)
    {
        const MaskP chk = (area > FloatP(0.f)) && (areaPdf > FloatP(0.f));
        if (!any(chk))
            return;
        const FloatP got = areaPdf * area;
        const FloatP tol =
            FloatP(1e-3f) * enoki::max(selPdf, FloatP(1e-6f));
        const MaskP bad = chk && (enoki::abs(got - selPdf) > tol);
        if (!any(bad))
            return;
        // Report the offending range only.
        LOG(LUX_WARNING, LUX_SYSTEM)
            << "BDPT(legacy): " << what << " area-density != selPdf ("
            << hmin(select(bad, got, INF_F)) << ".."
            << hmax(select(bad, got, FloatP(0.f)))
            << ", want " << selPdf << ")";
    }

    // A lobe reports reflectance (f > 0) but has no finite solid angle pdf.
    void BidirTallyGhost(BidirDiagLocal *d, const BSDFEvalP &ef,
                         MaskP evaluated, MaskP cont, const SWCSpectrumP &Lc,
                         const FloatP &w, const UInt32P &flags, int nE,
                         int nL)
    {
        if (!d)
            return;
        const MaskP hasF = ef.f > SWCSpectrumP(0.f);
        const MaskP hasPdf = ef.pdf > FloatP(0.f);
        const MaskP ghost = evaluated && hasF && !hasPdf;
        if (!any(ghost))
            return;
        const MaskP deltaOnly = has_flag(flags, BSDFType::Specular) &&
                                !has_flag(flags, BSDFType::Smooth);
        GhostCell &c = d->ghost[nE][nL];
        c.delta += uint64_t(count(ghost && deltaOnly));
        c.mixed += uint64_t(count(ghost && !deltaOnly));
        c.energy += double(enoki::hsum(select(cont && ghost, Lc * w,
                                              SWCSpectrumP(0.f))));
    }

    // Record contributions above the firefly threshold into the list.
    void BidirRecordFirefly(BidirDiagLocal *dl, float threshold,
                            const FloatP &x, const FloatP &y, MaskP cont,
                            const SWCSpectrumP &Lc, const FloatP &w,
                            const FloatP &d2, int s, int t,
                            const Breakdown *bd)
    {
        if (!dl)
            return;
        // Run before the threshold test.
        dl->recordCalls += 1;
        const float tileMax = enoki::hmax(select(cont, Lc * w, SWCSpectrumP(0.f)));
        if (tileMax > dl->maxLcw)
            dl->maxLcw = tileMax;
        if (threshold <= 0.f)
            return;
        const SWCSpectrumP lcw = Lc * w;
        const MaskP hit = cont && (lcw > FloatP(threshold));
        if (!any(hit))
            return;
        alignas(64) float mags[PACKET_WIDTH], lcs[PACKET_WIDTH];
        alignas(64) float ws[PACKET_WIDTH], d2s[PACKET_WIDTH];
        alignas(64) float xs[PACKET_WIDTH], ys[PACKET_WIDTH];
        // compress() binds its destination as Mem&.
        auto cmp = [&](float *dst, const FloatP &v)
        { enoki::compress(dst, v, hit); };
        float *pm = mags;
        const size_t c = enoki::compress(pm, lcw, hit);
        cmp(lcs, Lc);
        cmp(ws, w);
        cmp(d2s, d2);
        cmp(xs, x);
        cmp(ys, y);
        // Breakdown fields are packets too.
        alignas(64) float bBase[PACKET_WIDTH], bDOrg[PACKET_WIDTH];
        alignas(64) float bHLit[PACKET_WIDTH], bLExt[PACKET_WIDTH];
        alignas(64) float bEExt[PACKET_WIDTH], bDPath[PACKET_WIDTH];
        alignas(64) float bOkL[PACKET_WIDTH], bOkE[PACKET_WIDTH];
        if (bd)
        {
            cmp(bBase, bd->base);
            cmp(bDOrg, bd->directOrigin);
            cmp(bHLit, bd->hitLightAlt);
            cmp(bLExt, bd->lightExt);
            cmp(bEExt, bd->eyeExt);
            cmp(bDPath, bd->directPath);
            cmp(bOkL, select(bd->okLight, FloatP(1.f), FloatP(0.f)));
            cmp(bOkE, select(bd->okEye, FloatP(1.f), FloatP(0.f)));
        }
        for (size_t i = 0; i < c; ++i)
        {
            BidirDiagStore::Firefly f;
            f.lcw = mags[i];
            f.lc = lcs[i];
            f.w = ws[i];
            f.d2 = d2s[i];
            f.s = s;
            f.t = t;
            f.px = int(xs[i]);
            f.py = int(ys[i]);
            if (bd)
            {
                f.base = bBase[i];
                f.directOrigin = bDOrg[i];
                f.hitLightAlt = bHLit[i];
                f.lightExt = bLExt[i];
                f.eyeExt = bEExt[i];
                f.directPath = bDPath[i];
                f.okLight = bOkL[i] > 0.5f;
                f.okEye = bOkE[i] > 0.5f;
            }
            dl->fireflies.push_back(f);
        }
    }

    // Flush the store's tallies and firefly heap to the log.
    void BidirEndDump(const BidirDiagStore &store, float threshold)
    {
        std::lock_guard<std::mutex> lk(store.mtx);

        // Firefly probe.
        LOG(LUX_WARNING, LUX_SYSTEM)
            << "BDPT firefly probe: calls=" << store.recordCalls
            << " maxLcw=" << store.maxLcw
            << " threshold=" << threshold
            << " recorded=" << store.top.size();

        uint64_t totDelta = 0, totMixed = 0;
        for (size_t t = 0; t < store.ghost.size(); ++t)
            for (size_t s = 0; s < store.ghost[t].size(); ++s)
            {
                const GhostCell &c = store.ghost[t][s];
                if (c.delta == 0 && c.mixed == 0)
                    continue;
                totDelta += c.delta;
                totMixed += c.mixed;
                LOG(LUX_WARNING, LUX_SYSTEM)
                    << "BDPT diag: ghost (t=" << t << ",s=" << s
                    << ") delta=" << c.delta << " mixed=" << c.mixed
                    << " energy=" << c.energy;
            }
        LOG(LUX_WARNING, LUX_SYSTEM)
            << "BDPT diag: TOTAL ghost delta=" << totDelta
            << " mixed=" << totMixed
            << (totDelta > 0
                    ? " -- DELTA-ONLY ghosts present: connectibility bug"
                    : "");

        // Firefly top-N descending by |Lc*w|.
        std::vector<BidirDiagStore::Firefly> sorted = store.top;
        std::sort(sorted.begin(), sorted.end(),
                  [](const BidirDiagStore::Firefly &a,
                     const BidirDiagStore::Firefly &b)
                  { return a.lcw > b.lcw; });
        for (size_t i = 0; i < sorted.size(); ++i)
        {
            const auto &f = sorted[i];
            LOG(LUX_WARNING, LUX_SYSTEM)
                << "BDPT firefly #" << i << " (t=" << f.t << ",s=" << f.s
                << ") px=(" << f.px << "," << f.py << ") lcw=" << f.lcw
                << " lc=" << f.lc << " w=" << f.w << " d2=" << f.d2
                << " | base=" << f.base << " dirOrigin=" << f.directOrigin
                << " hitLightAlt=" << f.hitLightAlt
                << " lightExt=" << f.lightExt << " eyeExt=" << f.eyeExt
                << " directPath=" << f.directPath
                << " okL=" << int(f.okLight) << " okE=" << int(f.okEye);
        }
    }

    // Merge a tile's accumulator into the shared store under its mutex.
    void BidirMergeTile(BidirDiagStore &store, BidirDiagLocal &dl)
    {
        dl.TrimFireflies();
        std::lock_guard<std::mutex> lk(store.mtx);
        for (size_t t = 0; t < dl.ghost.size(); ++t)
            for (size_t s = 0; s < dl.ghost[t].size(); ++s)
            {
                GhostCell &g = store.ghost[t][s];
                g.delta += dl.ghost[t][s].delta;
                g.mixed += dl.ghost[t][s].mixed;
                g.energy += dl.ghost[t][s].energy;
            }
        store.recordCalls += dl.recordCalls;
        store.maxLcw = std::max(store.maxLcw, dl.maxLcw);
        for (const auto &f : dl.fireflies)
            store.InsertFirefly(f);
    }

} // namespace lux2

#endif // LUX2_INTEGRATOR_DEBUG
