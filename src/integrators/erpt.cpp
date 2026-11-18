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

#include "integrators/erpt.h"

#include "core/film.h"
#include "core/paramset.h"
#include "core/register.h"
#include "core/scene.h"

#include <algorithm>
#include <array>
#include <vector>

namespace lux2
{
    void ERPTIntegrator::Start(const Scene &scene)
    {
        BidirIntegrator::Start(scene);
        m_sh.params = m_opt.chain;
        m_sh.calSamples = m_opt.calSamples;
        m_sh.poolBytes = size_t(m_opt.poolMB) << 20;
        m_sh.smoothing = m_opt.smoothing;
        m_sh.smoothRadius = m_opt.smoothRadius;
        m_sh.frozen = false;
        m_sh.calEnergy = m_sh.calCount = 0.0;
        m_sh.autoEd = true;
        if (m_opt.edeposit > 0.f)
        {
            m_sh.autoEd = false;
            m_sh.Freeze(m_opt.edeposit);
        }
        // TODO: Scene::GetFilm() constness / XRes() accessor.
        if (m_opt.smoothing)
            m_sh.proposals.Init(scene.GetFilm().XRes(), scene.GetFilm().YRes());
        std::lock_guard<std::mutex> lk(m_regMu);
        m_cores.clear();
    }

    std::shared_ptr<ERPTIntegrator::Core>
    ERPTIntegrator::CoreFor(IntegratorWorkerState *ws)
    {
        std::lock_guard<std::mutex> lk(m_regMu);
        auto &p = m_cores[ws];
        if (!p)
            p = std::make_shared<Core>(m_sh);
        return p;
    }

    // Push the worker's sparse accumulator into the film.
    void ERPTIntegrator::Flush(Core &core)
    {
        erpt::SparseAccum &acc = core.sink.acc;
        if (acc.Empty() || !core.film)
            return;
        std::vector<int32_t> xs, ys;
        std::vector<float> xyz;
        xs.reserve(acc.Size());
        ys.reserve(acc.Size());
        xyz.reserve(acc.Size() * 3u);
        std::vector<std::array<float, 3>> props;
        acc.ForEach(
            [&](int32_t x, int32_t y, const float *v)
            {
                if (v[0] != 0.f || v[1] != 0.f || v[2] != 0.f)
                {
                    xs.push_back(x);
                    ys.push_back(y);
                    xyz.push_back(v[0]);
                    xyz.push_back(v[1]);
                    xyz.push_back(v[2]);
                }
                if (m_sh.smoothing && v[3] != 0.f)
                    props.push_back({float(x), float(y), v[3]});
            });
        if (!xs.empty())
            core.film->SplatLightSparse(xs.data(), ys.data(), xyz.data(), xs.size());
        if (!props.empty())
        {
            std::lock_guard<std::mutex> lk(m_gridMu);
            for (const auto &p : props)
                m_sh.proposals.Add(int(p[0]), int(p[1]), p[2]);
        }
        acc.Clear();
    }

    void ERPTIntegrator::OnTileBegin(const Scene &, const Tile &, WalkCtx &ctx,
                                     IntegratorWorkerState *ws, Film &dest,
                                     uint32_t)
    {
        const std::shared_ptr<Core> core = CoreFor(ws);
        core->film = &dest;
        core->ctx = &ctx;
        core->sink.calEnergy = 0.0;
        ctx.seedSink = &core->sink;
    }

    void ERPTIntegrator::OnTileEnd(const Scene &, const Tile &tile, WalkCtx &ctx,
                                   IntegratorWorkerState *ws, Film &, uint32_t spp)
    {
        const std::shared_ptr<Core> core = CoreFor(ws);
        // TODO: Tile extent field names.
        const double nSamples =
            double(tile.x1 - tile.x0) * double(tile.y1 - tile.y0) * double(spp);
        m_sh.AddCalibration(core->sink.calEnergy, nSamples);
        core->sink.calEnergy = 0.0;

        if (core->sink.pool.ShouldDrain())
            core->sink.Drain(ctx);
        // Flush every tile so writeinterval snapshots see deposited energy.
        Flush(*core);
    }

    void ERPTIntegrator::End(const Scene &scene)
    {
        // Seeds still pooled when the last tile finished must be run now, or
        // their energy is lost (bias). Uses each worker's last ctx.
        // TODO: confirm worker state lifetime vs End().
        {
            std::lock_guard<std::mutex> lk(m_regMu);
            for (auto &kv : m_cores)
            {
                Core &c = *kv.second;
                if (c.ctx && !c.sink.pool.Empty())
                    c.sink.Drain(*c.ctx);
                Flush(c);
                std::lock_guard<std::mutex> lk2(m_gridMu);
                m_sh.stats.Add(c.sink.stats);
            }
            m_cores.clear();
        }
        LOG(LUX_WARNING, LUX_SYSTEM)
            << "ERPT: seeds=" << m_sh.stats.seeds << " chains=" << m_sh.stats.chains
            << " steps=" << m_sh.stats.steps << " accepted=" << m_sh.stats.accepted
            << " valid=" << m_sh.stats.valid << " ed=" << m_sh.ed.load()
            << " clamped=" << m_sh.stats.chainClamped
            << " fxZero=" << m_sh.stats.fxZero
            << " seedGateFail=" << m_sh.stats.seedGateFail
            << " Ein=" << m_sh.stats.energyIn << " Eout=" << m_sh.stats.energyOut;
        BidirIntegrator::End(scene);
    }

    std::shared_ptr<SurfaceIntegrator> ERPTIntegrator::CreateSurfaceIntegrator(
        const PluginContext &ctx)
    {
        ParamSet *p = ctx.params;
        auto I = [&](const char *n, int d) { return p ? p->FindOneInt(n, d) : d; };
        auto F = [&](const char *n, float d) { return p ? p->FindOneFloat(n, d) : d; };
        auto B = [&](const char *n, bool d) { return p ? p->FindOneBool(n, d) : d; };

        int eyeDepth = std::min(I("eyedepth", 8), MAX_DEPTH);
        int lightDepth = std::min(I("lightdepth", 8), MAX_DEPTH);
        DiagConfig cfg;
        cfg.noMis = B("nomis", false);
        cfg.misPower = F("mispower", 2.f);
        cfg.diag = B("diag", false);
        cfg.threshold = F("diagthreshold", 0.f);
        cfg.wfSortByPath = B("wf_sortpath", false);

        Options o;
        // Mutations per chain; raise for more redistribution/smoothing.
        o.chain.m = uint32_t(std::max(1, I("mutationsperseed", 100)));
        // Consecutive-sample filter; reduces splotching from stuck chains.
        o.chain.maxConsec = uint32_t(std::max(0, I("maxconsecutive", 0)));
        // Spreads each chain's deposits over hues (0 disables).
        o.chain.pLambda = std::min(1.f, std::max(0.f, F("lambdamutation", 0.f)));
        // Put ed*a on the proposal and ed*(1-a) on the current
        // state instead of all ed on one.
        o.chain.splitDeposit = B("splitdeposit", true);
        // Samples accumulated before acalibrating (mean energy/sample
        // divided by m).
        o.calSamples = double(I("calsamples", 200000));
        // Manual deposition energy; 0 = calibrate from calSamples.
        o.edeposit = F("edeposit", 0.f);
        // Seed pool size per worker before it drains into chains.
        o.poolMB = std::max(1, I("seedpoolmb", 8));
        // Optional smoothing filter over the proposal grid.
        o.smoothing = B("smoothing", false);
        // Smoothing kernel radius.
        o.smoothRadius = I("smoothradius", 4);

        return std::make_shared<ERPTIntegrator>(
            o, eyeDepth, lightDepth, I("maxdepth", -1), F("eyerrthreshold", 0.f),
            F("lightrrthreshold", 0.f), B("debug", false), cfg,
            p ? p->FindOneString("lightstrategy", "one") : "one",
            p ? p->FindOneString("lightpathstrategy", "one") : "one",
            I("wf_samples", 128), I("wf_lightpool", 0), I("wf_highwater", 0),
            I("wf_minfill", -1));
    }

    LUX2_REGISTER_SURFACE_INTEGRATOR(ERPTIntegrator, "erpt");
} // namespace lux2
