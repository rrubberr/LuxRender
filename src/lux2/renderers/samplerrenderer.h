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

#ifndef LUX2_RENDERERS_SAMPLERRENDERER_H
#define LUX2_RENDERERS_SAMPLERRENDERER_H

#include "core/renderer.h"

#include <atomic>
#include <memory>

namespace lux2
{

    struct PluginContext;

    // Renderer state for cooperative cancellation.
    enum class RenderState
    {
        Run,
        Pause,
        Terminate
    };

    // Workers claim pixels per pass and splat directly into the film.
    class SamplerRenderer : public Renderer
    {
    public:
        SamplerRenderer() = default;

        void Render(const Scene &scene, SurfaceIntegrator &integrator) override;

        // Cooperative controls are safe to call from any thread while Render()
        // runs on another.
        void Pause() override { m_state.store(RenderState::Pause); }
        void Resume() override { m_state.store(RenderState::Run); }
        void Terminate() override { m_state.store(RenderState::Terminate); }
        bool IsRendering() const override
        {
            return m_state.load() == RenderState::Run;
        }
        RenderState State() const { return m_state.load(); }

        static std::shared_ptr<Renderer> CreateRenderer(const PluginContext &ctx);

    private:
        std::atomic<RenderState> m_state{RenderState::Run};
    };

} // namespace lux2

#endif // LUX2_RENDERERS_SAMPLERRENDERER_H
