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

#ifndef LUX2_INTEGRATORS_PATH_H
#define LUX2_INTEGRATORS_PATH_H

#include "core/color.h"
#include "core/integrator.h"

#include <memory>
#include <string>

namespace lux2
{

    struct PluginContext;
    class Scene;

    // Eye only path tracer.
    class PathIntegrator : public SurfaceIntegrator
    {
    public:
        // Transport estimator selection (debug):
        enum class LightMode
        {
            MIS,
            NEE,
            BSDF
        };

        PathIntegrator(int maxDepth, float rrContinueProb,
                       const std::string &rrStrategy,
                       bool includeEnvironment, bool directLightSampling,
                       LightMode lightMode = LightMode::MIS)
            : m_maxDepth(maxDepth),
              m_rrContinueProb(rrContinueProb),
              m_rrStrategy(rrStrategy),
              m_includeEnvironment(includeEnvironment),
              m_directLightSampling(directLightSampling),
              m_lightMode(lightMode) {}

        int PassCount() const override { return 1; }

        void Start(const Scene &scene) override;

        void RenderTile(const Scene &scene, const Tile &tile,
                        Film &dest, Sampler &sampler,
                        uint64_t sampleBasis, uint32_t spp) override;

        // Accessors.
        int MaxDepth() const { return m_maxDepth; }
        float RRContinueProb() const { return m_rrContinueProb; }
        const std::string &RRStrategy() const { return m_rrStrategy; }
        bool IncludeEnvironment() const { return m_includeEnvironment; }
        bool DirectLightSampling() const { return m_directLightSampling; }
        LightMode GetLightMode() const { return m_lightMode; }

        static std::shared_ptr<SurfaceIntegrator> CreateSurfaceIntegrator(
            const PluginContext &ctx);

    private:
        // Packet random walk. When lemOut is real it receives the
        // emission on hit term only for the debug AOV)
        SWCSpectrumP WalkPath(const Scene &scene, Sampler &sampler,
                              const RayP &primary,
                              const SpectrumWavelengthsP &sw,
                              FloatP *alpha,
                              SWCSpectrumP *lemOut = nullptr) const;

        int m_maxDepth;
        float m_rrContinueProb;
        std::string m_rrStrategy;
        bool m_includeEnvironment;
        bool m_directLightSampling;
        LightMode m_lightMode;

        const Scene *m_scene = nullptr;
    };

} // namespace lux2

#endif // LUX2_INTEGRATORS_PATH_H
