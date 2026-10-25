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

#ifndef LUX2_INTEGRATORS_BIDIRECTIONAL_H
#define LUX2_INTEGRATORS_BIDIRECTIONAL_H

#include "core/integrator.h"
#include "core/renderinghints.h"
#include "integrators/debug/integrator_debug.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace lux2
{

    struct PluginContext;
    class Scene;

    // Connection strategy identifiers.
    enum class Strat : int
    {
        HitLight = 0, // eye subpath terminated on an emitter
        NEE = 1,      // ConnectDirect (t>=2) / ConnectCameraDirect (t==1)
        Origin = 2,   // ConnectGeneral with nL == 1
        General = 3,  // ConnectGeneral with nL >= 2
    };

    // Diagnostic configuration bundle.
    struct DiagConfig
    {
        int onlyS = -1;        // -1 = all; else keep only this light vertex count
        int onlyT = -1;        // -1 = all; else keep only this eye vertex count (nE)
        bool noMis = false;    // w = 1 (isolates each strategy's Lc)
        float misPower = 2.f;  // exponent p in the power heuristic (0,1,2)
        bool diag = false;     // enable DiagStore tallies and firefly recorder
        float threshold = 0.f; // firefly record threshold (0 = off)
    };

    class BidirIntegrator : public SurfaceIntegrator
    {
    public:
        BidirIntegrator(int eyeDepth, int lightDepth, int maxDepth,
                        float eyeRRThreshold, float lightRRThreshold,
                        bool useT1, bool debug, const DiagConfig &diagCfg,
                        const std::string &lightStrategy = "one",
                        const std::string &lightPathStrategy = "one")
            : m_eyeDepth(eyeDepth),
              m_lightDepth(lightDepth),
              m_maxDepth(maxDepth),
              m_eyeRRThreshold(eyeRRThreshold),
              m_lightRRThreshold(lightRRThreshold),
              m_useT1(useT1),
              m_debug(debug),
              m_diagCfg(diagCfg),
              m_lightStrategy(lightStrategy),
              m_lightPathStrategy(lightPathStrategy)
        {
            // Total depth clamp.
            if (m_maxDepth < 0)
                m_maxDepth = m_eyeDepth + m_lightDepth;
        }

        ~BidirIntegrator() override;

        int PassCount() const override { return 1; }

        bool SupportsMedia() const override { return true; }

        void Start(const Scene &scene) override;

        void RenderTile(const Scene &scene, const Tile &tile,
                        Film &dest, Sampler &sampler,
                        uint64_t sampleBasis, uint32_t spp) override;

        // Flush and free the debug buffers.
        void End(const Scene &scene) override;

        // Accessors.
        int EyeDepth() const { return m_eyeDepth; }
        int LightDepth() const { return m_lightDepth; }
        int MaxDepth() const { return m_maxDepth; }
        bool UseT1() const { return m_useT1; }
        bool Debug() const { return m_debug; }
        const std::string &LightStrategy() const { return m_lightStrategy; }
        const std::string &LightPathStrategy() const
        {
            return m_lightPathStrategy;
        }

        static std::shared_ptr<SurfaceIntegrator> CreateSurfaceIntegrator(
            const PluginContext &ctx);

    private:
        int m_eyeDepth;
        int m_lightDepth;
        int m_maxDepth; // total s+t-2 clamp (derived if <0)
        float m_eyeRRThreshold;
        float m_lightRRThreshold;
        bool m_useT1;
        bool m_debug;
        std::string m_lightStrategy;
        std::string m_lightPathStrategy;

        // Light sampling tables.
        std::vector<LightsSamplingStrategy> m_lsD;
        std::vector<LightsSamplingStrategy> m_lsP;

        const Scene *m_scene = nullptr;
        // So diagnostics cannot flood the log.
        std::atomic<bool> m_warnedLights{false};
#ifdef LUX2_INTEGRATOR_DEBUG
        std::atomic<bool> m_densityChecked{false};
#endif

        DiagConfig m_diagCfg;
#ifdef LUX2_INTEGRATOR_DEBUG
        // Diagnostic tallies and firefly heap.
        std::unique_ptr<BidirDiagStore> m_diagStore;
#endif
    };

} // namespace lux2

#endif // LUX2_INTEGRATORS_BIDIRECTIONAL_H
