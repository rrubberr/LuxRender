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

#include "core/context2.h"

#include "core/displaytimer.h"
#include "core/film.h"
#include "core/sampler.h"
#include "core/threadpool.h"

namespace lux2
{

    Context2 *Context2::s_active = nullptr;

    // ---------------------------------------------------------------------------
    // Lifecycle
    // ---------------------------------------------------------------------------
    void Context2::Init()
    {
        m_state = LUX2_STATE_OPTIONS_BLOCK;
        m_desc = SceneDescription();
        m_xform = TransformStack();
        m_gs.clear();
        m_gs.emplace_back();
        m_shapeNo = 0;
        m_aborted.store(false);
        m_terminated.store(false);
    }

    void Context2::Cleanup()
    {
        // Stop rendering.
        m_aborted.store(true);
        m_terminated.store(true);
        if (m_scene)
            m_scene->GetRenderer().Terminate();
        JoinRenderThread();

        // Destroy the statistics object.
        m_stats.reset();
        m_registry.Clear();
        m_scene.reset();
        m_gs.clear();
        m_desc = SceneDescription();
        m_state = LUX2_STATE_UNINITIALIZED;
    }

    bool Context2::RequireWorld(const char *what)
    {
        if (m_state != LUX2_STATE_WORLD_BLOCK)
        {
            LOG(LUX_ERROR, LUX_NOTPRIMS) << what << " must be called between WorldBegin and WorldEnd";
            return false;
        }
        return true;
    }

    bool Context2::RequireOptions(const char *what)
    {
        if (m_state != LUX2_STATE_OPTIONS_BLOCK)
        {
            LOG(LUX_ERROR, LUX_NOTOPTIONS) << what << " must be called before WorldBegin";
            return false;
        }
        return true;
    }

    bool Context2::RequireInitialized(const char *what)
    {
        if (m_state == LUX2_STATE_UNINITIALIZED || m_state == LUX2_STATE_PARSE_FAIL)
        {
            LOG(LUX_ERROR, LUX_NOTSTARTED) << what << " called on an uninitialized context";
            return false;
        }
        return true;
    }

    // ---------------------------------------------------------------------------
    // Options block statements
    // ---------------------------------------------------------------------------
    void Context2::Renderer(const std::string &name, const ParamSet &params)
    {
        if (!RequireOptions("Renderer"))
            return;
        m_desc.rendererName = name;
        m_desc.rendererParams = params;
    }

    void Context2::Sampler(const std::string &name, const ParamSet &params)
    {
        if (!RequireOptions("Sampler"))
            return;
        m_desc.samplerName = name;
        m_desc.samplerParams = params;
    }

    void Context2::Accelerator(const std::string &name, const ParamSet &params)
    {
        // We always use Embree.
        (void)name;
        (void)params;
    }

    void Context2::Film(const std::string &name, const ParamSet &params)
    {
        if (!RequireOptions("Film"))
            return;
        m_desc.filmName = name;
        m_desc.filmParams = params;
    }

    void Context2::PixelFilter(const std::string &name, const ParamSet &params)
    {
        if (!RequireOptions("PixelFilter"))
            return;
        m_desc.filterName = name;
        m_desc.filterParams = params;
    }

    void Context2::Camera(const std::string &name, const ParamSet &params)
    {
        if (!RequireOptions("Camera"))
            return;
        m_desc.cameraName = name;
        m_desc.cameraParams = params;
        m_desc.cameraTransform = m_xform.current();
    }

    void Context2::SurfaceIntegrator(const std::string &name, const ParamSet &params)
    {
        if (!RequireOptions("SurfaceIntegrator"))
            return;
        m_desc.surfIntName = name;
        m_desc.surfIntParams = params;
    }

    void Context2::VolumeIntegrator(const std::string &name, const ParamSet &params)
    {
        if (!RequireOptions("VolumeIntegrator"))
            return;
        m_desc.volIntName = name;
        m_desc.volIntParams = params;
    }

    // ---------------------------------------------------------------------------
    // World block statements
    // ---------------------------------------------------------------------------
    void Context2::Material(const std::string &name, const ParamSet &params)
    {
        if (!RequireWorld("Material"))
            return;
        MaterialBinding b;
        b.isNamed = false;
        b.pluginName = name;
        b.params = params;
        gs().material = b;
    }

    void Context2::MakeNamedMaterial(const std::string &name, const ParamSet &params)
    {
        if (!RequireWorld("MakeNamedMaterial"))
            return;
        if (gs().namedMaterials.find(name) == gs().namedMaterials.end() &&
            m_desc.namedMaterials.find(name) == m_desc.namedMaterials.end())
        {
            // First definition in this scope.
            m_desc.namedMaterials[name] = params;
        }
        else
        {
            LOG(LUX_WARNING, LUX_NOERROR) << "Named material '" << name << "' being redefined";
        }
        gs().namedMaterials[name] = params;
    }

    void Context2::NamedMaterial(const std::string &name)
    {
        if (!RequireWorld("NamedMaterial"))
            return;
        if (gs().namedMaterials.find(name) == gs().namedMaterials.end() &&
            m_desc.namedMaterials.find(name) == m_desc.namedMaterials.end())
        {
            LOG(LUX_ERROR, LUX_BADHANDLE) << "Named material '" << name << "' unknown";
            return;
        }
        MaterialBinding b;
        b.isNamed = true;
        b.namedRef = name;
        gs().material = b;
    }

    void Context2::Texture(const std::string &texName, const std::string &texType,
                           const std::string &pluginName, const ParamSet &params)
    {
        if (!RequireWorld("Texture"))
            return;
        TextureDesc d{texType, pluginName, params};
        m_desc.textures[texName] = d;
        if (texType == "float")
            gs().floatTextures[texName] = d;
        else if (texType == "color")
            gs().colorTextures[texName] = d;
        else if (texType == "fresnel")
            gs().fresnelTextures[texName] = d;
        else
            LOG(LUX_ERROR, LUX_SYNTAX) << "Unknown texture type '" << texType << "'";
    }

    void Context2::LightSource(const std::string &name, const ParamSet &params)
    {
        if (!RequireWorld("LightSource"))
            return;
        LightDesc d;
        d.name = name;
        d.params = params;
        d.toWorld = m_xform.current();
        d.lightGroup = gs().currentLightGroup;
        m_desc.lights.push_back(d);
    }

    void Context2::AreaLightSource(const std::string &name, const ParamSet &params)
    {
        if (!RequireWorld("AreaLightSource"))
            return;
        gs().areaLightActive = true;
        gs().areaLightName = name;
        gs().areaLightParams = params;
    }

    void Context2::LightGroup(const std::string &name, const ParamSet &params)
    {
        if (!RequireWorld("LightGroup"))
            return;
        (void)params;
        gs().currentLightGroup = name;
    }

    void Context2::Shape(const std::string &name, const ParamSet &params)
    {
        if (!RequireWorld("Shape"))
            return;
        ShapeDesc d;
        d.name = name;
        d.params = params;
        d.toWorld = m_xform.current();
        d.material = gs().material;

        if (gs().areaLightActive)
        {
            d.isAreaLight = true;
            d.areaLightName = gs().areaLightName;
            d.areaLightParams = gs().areaLightParams;
            d.lightGroup = gs().currentLightGroup;
            // An area light applies to exactly one following shape.
            gs().areaLightActive = false;
        }
        m_desc.shapes.push_back(std::move(d));
        ++m_shapeNo;
    }

    // ---------------------------------------------------------------------------
    // Attribute / transform state
    // ---------------------------------------------------------------------------
    void Context2::AttributeBegin()
    {
        if (!RequireWorld("AttributeBegin"))
            return;
        m_gs.push_back(gs());
    }

    void Context2::AttributeEnd()
    {
        if (!RequireWorld("AttributeEnd"))
            return;
        if (m_gs.size() > 1)
            m_gs.pop_back();
        else
            LOG(LUX_ERROR, LUX_NESTING) << "AttributeEnd without matching AttributeBegin";
    }

    void Context2::TransformBegin()
    {
        if (!RequireWorld("TransformBegin"))
            return;
        m_xform.push();
    }

    void Context2::TransformEnd()
    {
        if (!RequireWorld("TransformEnd"))
            return;
        m_xform.pop();
    }

    void Context2::Translate(float dx, float dy, float dz)
    {
        if (!RequireInitialized("Translate"))
            return;
        m_xform.postMultiply(Transform::translate(Vector3f(dx, dy, dz)));
    }

    void Context2::Rotate(float angle, float ax, float ay, float az)
    {
        if (!RequireInitialized("Rotate"))
            return;
        m_xform.postMultiply(Transform::rotate(Vector3f(ax, ay, az), angle));
    }

    void Context2::Scale(float sx, float sy, float sz)
    {
        if (!RequireInitialized("Scale"))
            return;
        m_xform.postMultiply(Transform::scale(Vector3f(sx, sy, sz)));
    }

    void Context2::Transform(const float m[16])
    {
        if (!RequireInitialized("Transform"))
            return;
        m_xform.set(Transform::from_column_major(m));
    }

    void Context2::ConcatTransform(const float m[16])
    {
        if (!RequireInitialized("ConcatTransform"))
            return;
        m_xform.postMultiply(Transform::from_column_major(m));
    }

    void Context2::Identity()
    {
        if (!RequireInitialized("Identity"))
            return;
        m_xform.identity();
    }

    void Context2::LookAt(float ex, float ey, float ez, float lx, float ly, float lz,
                          float ux, float uy, float uz)
    {
        if (!RequireInitialized("LookAt"))
            return;
        m_xform.postMultiply(Transform::look_at(
            Point3f(ex, ey, ez), Point3f(lx, ly, lz), Vector3f(ux, uy, uz)));
    }

    void Context2::CoordinateSystem(const std::string &name)
    {
        if (!RequireInitialized("CoordinateSystem"))
            return;
        m_xform.nameCoordinateSystem(name);
    }

    void Context2::CoordSysTransform(const std::string &name)
    {
        if (!RequireInitialized("CoordSysTransform"))
            return;
        if (!m_xform.useCoordinateSystem(name))
            LOG(LUX_ERROR, LUX_BADHANDLE) << "Named coordinate system '" << name << "' unknown";
    }

    void Context2::ReverseOrientation()
    {
        if (!RequireWorld("ReverseOrientation"))
            return;
        gs().reverseOrientation = !gs().reverseOrientation;
    }

    // ---------------------------------------------------------------------------
    // World lifecycle
    // ---------------------------------------------------------------------------
    void Context2::WorldBegin()
    {
        if (!RequireOptions("WorldBegin"))
            return;
        m_state = LUX2_STATE_WORLD_BLOCK;
        m_xform = TransformStack();
        m_gs.clear();
        m_gs.emplace_back();
        m_shapeNo = 0;
    }

    void Context2::WorldEnd()
    {
        if (!RequireWorld("WorldEnd"))
            return;
        m_state = LUX2_STATE_OPTIONS_BLOCK;

        // Build from the parsed description.
        m_aborted.store(false);
        m_terminated.store(false);
        m_scene = std::make_unique<Scene>();
        m_scene->Commit(m_desc);

        // Statistics reference the committed film.
        if (m_scene->IsCommitted())
        {
            const double targetSpp = double(m_scene->GetSampler().SampleCount());
            m_stats = std::make_unique<RenderStatistics>(
                m_scene->GetFilm(), targetSpp, RenderThreadPool::Get().Count());
            m_registry.Insert(m_stats.get());
            m_registry.Insert(m_stats->formattedLong.get());
            m_registry.Insert(m_stats->formattedShort.get());
        }

        if (ShouldStartRenderingAfterParse())
            StartRendering();
    }

    void Context2::ParseEnd()
    {
        m_state = LUX2_STATE_OPTIONS_BLOCK;
    }

    // ---------------------------------------------------------------------------
    // Render orchestration
    // ---------------------------------------------------------------------------
    void Context2::StartRendering()
    {
        if (!m_scene || !m_scene->IsCommitted())
            return;
        std::lock_guard<std::mutex> lock(m_threadMutex);
        if (m_renderThread.joinable())
            return; // already running
        m_aborted.store(false);
        m_terminated.store(false);
        m_renderThread = std::thread([this]
                                     { RenderThreadMain(); });
    }

    void Context2::RenderThreadMain()
    {
        class Renderer &renderer = m_scene->GetRenderer();
        class Film &film = m_scene->GetFilm();

        if (m_stats)
            m_stats->Reset();

        DisplayTimer timer(film, m_stats.get());
        timer.Start();
        if (m_stats)
            m_stats->Start();

        renderer.Render(*m_scene, m_scene->GetSurfaceIntegrator());

        if (m_stats)
            m_stats->Stop();
        timer.Stop();

        // Skip file output when render is aborted.
        if (!m_aborted.load())
            film.WriteImage(ImageType(IMAGE_FINAL | IMAGE_FILE_ALL | IMAGE_FRAMEBUFFER));
    }

    void Context2::JoinRenderThread()
    {
        std::lock_guard<std::mutex> lock(m_threadMutex);
        if (m_renderThread.joinable())
            m_renderThread.join();
    }

    void Context2::Pause()
    {
        if (m_scene)
            m_scene->GetRenderer().Pause();
        // Exclude paused time.
        if (m_stats)
            m_stats->Stop();
    }

    void Context2::Resume()
    {
        if (m_scene)
            m_scene->GetRenderer().Resume();
        if (m_stats)
            m_stats->Start();
    }

    void Context2::Exit()
    {
        m_terminated.store(true);
        if (m_scene)
            m_scene->GetRenderer().Terminate();
    }

    void Context2::Abort()
    {
        m_aborted.store(true);
        Exit();
    }

    void Context2::Wait()
    {
        JoinRenderThread();
    }

    bool Context2::IsRendering() const
    {
        return m_scene && m_scene->GetRenderer().IsRendering();
    }

    // ---------------------------------------------------------------------------
    // Framebuffer forwarding
    // ---------------------------------------------------------------------------
    void Context2::UpdateFramebuffer()
    {
        if (m_scene)
            m_scene->GetFilm().UpdateFrameBuffer();
    }

    unsigned char *Context2::Framebuffer()
    {
        return m_scene ? m_scene->GetFilm().GetFrameBuffer() : nullptr;
    }

    float *Context2::FloatFramebuffer()
    {
        return m_scene ? m_scene->GetFilm().GetFloatFrameBuffer() : nullptr;
    }

    float *Context2::AlphaBuffer()
    {
        return m_scene ? m_scene->GetFilm().GetAlphaBuffer() : nullptr;
    }

    // ---------------------------------------------------------------------------
    // Unsupported
    // ---------------------------------------------------------------------------
    void Context2::Volume(const std::string &name, const ParamSet &params)
    {
        (void)name;
        (void)params;
        LOG(LUX_WARNING, LUX_UNIMPLEMENT) << LUX2_UNSUPPORTED_TAG << " Volume";
    }

    void Context2::MakeNamedVolume(const std::string &id, const std::string &name,
                                   const ParamSet &params)
    {
        (void)id;
        (void)name;
        (void)params;
        LOG(LUX_WARNING, LUX_UNIMPLEMENT) << LUX2_UNSUPPORTED_TAG << " MakeNamedVolume";
    }

    void Context2::Exterior(const std::string &name)
    {
        (void)name;
        LOG(LUX_WARNING, LUX_UNIMPLEMENT) << LUX2_UNSUPPORTED_TAG << " Exterior";
    }

    void Context2::Interior(const std::string &name)
    {
        (void)name;
        LOG(LUX_WARNING, LUX_UNIMPLEMENT) << LUX2_UNSUPPORTED_TAG << " Interior";
    }

    void Context2::PortalShape(const std::string &name, const ParamSet &params)
    {
        (void)name;
        (void)params;
        LOG(LUX_WARNING, LUX_UNIMPLEMENT) << LUX2_UNSUPPORTED_TAG << " PortalShape";
    }

    void Context2::MotionBegin(unsigned int n, const float *times)
    {
        (void)n;
        (void)times;
        LOG(LUX_WARNING, LUX_UNIMPLEMENT) << LUX2_UNSUPPORTED_TAG << " MotionBegin";
    }

    void Context2::MotionEnd()
    {
        LOG(LUX_WARNING, LUX_UNIMPLEMENT) << LUX2_UNSUPPORTED_TAG << " MotionEnd";
    }

    void Context2::ObjectBegin(const std::string &name)
    {
        (void)name;
        LOG(LUX_WARNING, LUX_UNIMPLEMENT) << LUX2_UNSUPPORTED_TAG << " ObjectBegin";
    }

    void Context2::ObjectEnd()
    {
        LOG(LUX_WARNING, LUX_UNIMPLEMENT) << LUX2_UNSUPPORTED_TAG << " ObjectEnd";
    }

    void Context2::ObjectInstance(const std::string &name)
    {
        (void)name;
        LOG(LUX_WARNING, LUX_UNIMPLEMENT) << LUX2_UNSUPPORTED_TAG << " ObjectInstance";
    }

} // namespace lux2
