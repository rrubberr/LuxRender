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

#ifndef LUX2_CONTEXT2_H
#define LUX2_CONTEXT2_H

#include "core/graphicsstate.h"
#include "core/paramset.h"
#include "core/queryableregistry.h"
#include "core/renderstats.h"
#include "core/scene.h"
#include "core/transform_stack.h"

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace lux2
{

    // Parser state machine).
    enum ContextState
    {
        LUX2_STATE_UNINITIALIZED = 0,
        LUX2_STATE_OPTIONS_BLOCK = 1, // before WorldBegin
        LUX2_STATE_WORLD_BLOCK = 2,   // between WorldBegin and WorldEnd
        LUX2_STATE_PARSE_FAIL = 3
    };

    class Context2
    {
    public:
        Context2() { Init(); }
        ~Context2() { Cleanup(); }

        // Active context get/set.
        static Context2 *GetActive() { return s_active; }
        static void SetActive(Context2 *c) { s_active = c; }

        // The committed scene, or nullptr before WorldEnd / after Cleanup.
        static Scene *GetCurrentScene()
        {
            Context2 *c = s_active;
            return c ? c->m_scene.get() : nullptr;
        }

        void Init();
        void Cleanup();

        int State() const { return m_state; }
        SceneDescription &Description() { return m_desc; }
        const SceneDescription &Description() const { return m_desc; }

        // Queryable attribute registry for the C-API. Objects register
        // themselves on creation and unregister on destruction.
        QueryableRegistry &Registry() { return m_registry; }
        const QueryableRegistry &Registry() const { return m_registry; }

        // Render statistics, created when a scene commits. Null until then.
        RenderStatistics *Stats() { return m_stats.get(); }

        // The committed scene.
        Scene *GetScene() { return m_scene.get(); }
        bool IsCommitted() const { return m_scene && m_scene->IsCommitted(); }

        // Reproducibility flags.
        void EnableDebugMode() { m_debugMode = true; }
        void DisableRandomMode() { m_randomMode = false; }
        bool DebugMode() const { return m_debugMode; }
        bool RandomMode() const { return m_randomMode; }

        // Render control.
        void Pause();
        void Resume();
        void Exit();
        void Abort();
        // Block until render thread finishes.
        void Wait();
        bool IsRendering() const;
        bool IsAborted() const { return m_aborted.load(); }

        // Framebuffer access.
        void UpdateFramebuffer();
        unsigned char *Framebuffer();
        float *FloatFramebuffer();
        float *AlphaBuffer();

        // Options block statements.
        void Renderer(const std::string &name, const ParamSet &params);
        void Sampler(const std::string &name, const ParamSet &params);
        void Accelerator(const std::string &name, const ParamSet &params);
        void Film(const std::string &name, const ParamSet &params);
        void PixelFilter(const std::string &name, const ParamSet &params);
        void Camera(const std::string &name, const ParamSet &params);
        void SurfaceIntegrator(const std::string &name, const ParamSet &params);
        void VolumeIntegrator(const std::string &name, const ParamSet &params);

        // World block statements.
        void Material(const std::string &name, const ParamSet &params);
        void MakeNamedMaterial(const std::string &name, const ParamSet &params);
        void NamedMaterial(const std::string &name);
        void Texture(const std::string &texName, const std::string &texType,
                     const std::string &pluginName, const ParamSet &params);
        void LightSource(const std::string &name, const ParamSet &params);
        void AreaLightSource(const std::string &name, const ParamSet &params);
        void LightGroup(const std::string &name, const ParamSet &params);
        void Shape(const std::string &name, const ParamSet &params);

        // Attribute / transform state.
        void AttributeBegin();
        void AttributeEnd();
        void TransformBegin();
        void TransformEnd();
        void Translate(float dx, float dy, float dz);
        void Rotate(float angle, float ax, float ay, float az);
        void Scale(float sx, float sy, float sz);
        void Transform(const float m[16]);
        void ConcatTransform(const float m[16]);
        void Identity();
        void LookAt(float ex, float ey, float ez, float lx, float ly, float lz,
                    float ux, float uy, float uz);
        void CoordinateSystem(const std::string &name);
        void CoordSysTransform(const std::string &name);
        void ReverseOrientation();

        // World lifecycle.
        void WorldBegin();
        void WorldEnd();
        void ParseEnd();

        // Parse control.
        void Free() { Cleanup(); }
        void MarkParseFail() { m_state = LUX2_STATE_PARSE_FAIL; }
        void StartRenderingAfterParse(bool start) { m_startRenderingAfterParse = start; }
        bool ShouldStartRenderingAfterParse() const { return m_startRenderingAfterParse; }

        // Unsupported.
        void Volume(const std::string &name, const ParamSet &params);
        void MakeNamedVolume(const std::string &id, const std::string &name,
                             const ParamSet &params);
        void Exterior(const std::string &name);
        void Interior(const std::string &name);
        void PortalShape(const std::string &name, const ParamSet &params);
        void MotionBegin(unsigned int n, const float *times);
        void MotionEnd();
        void ObjectBegin(const std::string &name);
        void ObjectEnd();
        void ObjectInstance(const std::string &name);

    private:
        // Guard helpers.
        bool RequireWorld(const char *what);
        bool RequireOptions(const char *what);
        bool RequireInitialized(const char *what);

        // Resolve the active light group to an index into
        // m_desc.lightGroups.
        std::uint32_t ResolveLightGroup();

        // Spawn the render thread.
        void StartRendering();
        // Statistics, display timer, write.
        void RenderThreadMain();
        // Join the render thread if it is running.
        void JoinRenderThread();

        static Context2 *s_active;

        int m_state = LUX2_STATE_UNINITIALIZED;
        SceneDescription m_desc;
        TransformStack m_xform;

        // Graphics state stack.
        std::vector<GraphicsState> m_gs;
        GraphicsState &gs() { return m_gs.back(); }

        unsigned int m_shapeNo = 0; // anonymous shape counter
        bool m_startRenderingAfterParse = true;

        // Reproducibility (legacy parity): debugMode or !randomMode pins the
        // base seed. randomMode also selects decorrelated group passes.
        bool m_debugMode = false;
        bool m_randomMode = true;
        // Progressive round counter, advanced once per render invocation.
        int m_renderRound = 0;

        QueryableRegistry m_registry;
        // Created when a scene commits.
        std::unique_ptr<RenderStatistics> m_stats;

        // Scene committed at WorldEnd.
        std::unique_ptr<Scene> m_scene;
        // Runs the renderer when auto start is enabled.
        std::thread m_renderThread;
        // Guards join().
        std::mutex m_threadMutex;
        std::atomic<bool> m_aborted{false};
        std::atomic<bool> m_terminated{false};
    };

} // namespace lux2

#endif // LUX2_CONTEXT2_H
