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

#ifndef LUX2_INTEGRATORS_ERPT_H
#define LUX2_INTEGRATORS_ERPT_H

#include "integrators/bidirectional.h"
#include "integrators/erpt/seed_capture.h"

#include <memory>
#include <mutex>
#include <unordered_map>

namespace lux2
{
    class ERPTIntegrator : public BidirIntegrator
    {
    public:
        struct Options
        {
            erpt::ChainParams chain;
            double calSamples = 20000.0;
            float edeposit = 0.f; // 0 = auto calibrate
            int poolMB = 8;
            bool smoothing = false;
            int smoothRadius = 4;
        };

        ERPTIntegrator(const Options &o, int eyeDepth, int lightDepth, int maxDepth,
                       float eyeRR, float lightRR, bool debug,
                       const DiagConfig &diag, const std::string &ls,
                       const std::string &lps, int wfSamples, int wfLightPool,
                       int wfHighWater, int wfMinFill)
            : BidirIntegrator(eyeDepth, lightDepth, maxDepth, eyeRR, lightRR, debug,
                              diag, ls, lps, wfSamples, wfLightPool, wfHighWater,
                              wfMinFill),
              m_opt(o)
        {
        }

        void Start(const Scene &scene) override;
        void End(const Scene &scene) override;

        static std::shared_ptr<SurfaceIntegrator> CreateSurfaceIntegrator(
            const PluginContext &ctx);

    protected:
        void OnTileBegin(const Scene &, const Tile &, WalkCtx &,
                         IntegratorWorkerState *, Film &, uint32_t spp) override;
        void OnTileEnd(const Scene &, const Tile &, WalkCtx &,
                       IntegratorWorkerState *, Film &, uint32_t spp) override;

    private:
        struct Core
        {
            explicit Core(erpt::ErptShared &sh) : sink(sh) {}
            erpt::ErptSeedSink sink;
            WalkCtx *ctx = nullptr; // worker state ctx
            Film *film = nullptr;
            double samples = 0.0;
        };
        std::shared_ptr<Core> CoreFor(IntegratorWorkerState *ws);
        void Flush(Core &core);

        Options m_opt;
        erpt::ErptShared m_sh;
        std::mutex m_regMu;
        std::unordered_map<IntegratorWorkerState *, std::shared_ptr<Core>> m_cores;
        std::mutex m_gridMu;
    };
} // namespace lux2

#endif // LUX2_INTEGRATORS_ERPT_H
