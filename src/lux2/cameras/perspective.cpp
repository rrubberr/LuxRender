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

#include "cameras/perspective.h"

#include "core/dynload.h"
#include "core/math.h"
#include "core/paramset.h"
#include "core/register.h"

#include <algorithm>
#include <cmath>

namespace lux2
{

    PerspectiveCamera::PerspectiveCamera(const Transform &cameraToWorld,
                                         const float screen[4], float hither,
                                         float yon, float shutterOpen,
                                         int xRes, int yRes, float fovDeg)
        : m_cameraToWorld(cameraToWorld),
          m_hither(hither), m_yon(yon), m_shutterOpen(shutterOpen),
          m_xRes(xRes), m_yRes(yRes)
    {
        // Film plane window at z == 1.
        m_screenX0 = screen[0];
        m_screenX1 = screen[1];
        m_screenY0 = screen[2];
        m_screenY1 = screen[3];

        // Origin and forward normal in world space.
        m_pos = m_cameraToWorld * Point3fP(FloatP(0.f), FloatP(0.f), FloatP(0.f));
        m_normal = normalize(m_cameraToWorld *
                             Vector3fP(FloatP(0.f), FloatP(0.f), FloatP(1.f)));

        // Physical pixel area at the film plane.
        m_tanHalf = std::tan(fovDeg * (PI / 180.f) * 0.5f);
        const float xPixelWidth =
            2.f * m_tanHalf * (screen[1] - screen[0]) * 0.5f / float(xRes);
        const float yPixelHeight =
            2.f * m_tanHalf * (screen[3] - screen[2]) * 0.5f / float(yRes);
        m_apixel = xPixelWidth * yPixelHeight;
    }

    void PerspectiveCamera::GenerateRay(const FloatP &x, const FloatP &y,
                                        const FloatP &time, RayP *ray,
                                        FloatP *weight, MaskP active) const
    {
        // Raster -> film-plane screen coords.
        const FloatP fx = x / FloatP(float(m_xRes));
        const FloatP fy = y / FloatP(float(m_yRes));
        const FloatP screenX = m_screenX0 + (m_screenX1 - m_screenX0) * fx;
        const FloatP screenY = m_screenY1 + (m_screenY0 - m_screenY1) * fy;

        // Camera space direction to the film plane at z == 1.
        const FloatP tanHalf(m_tanHalf);
        const Vector3fP dirCam(screenX * tanHalf, screenY * tanHalf, FloatP(1.f));

        // To world, normalize.
        Vector3fP dirWorld = normalize(m_cameraToWorld * dirCam);

        enoki::masked(ray->o, active) = m_pos;
        enoki::masked(ray->d, active) = dirWorld;

        // Clip along the view axis t such that (t * dirWorld).z_cam == hither/yon.
        const FloatP cosT = dot(dirWorld, m_normal);
        const FloatP safeCos = select(cosT > FloatP(EPS_DENOM), cosT, FloatP(EPS_DENOM));
        enoki::masked(ray->mint, active) = FloatP(m_hither) / safeCos;
        enoki::masked(ray->maxt, active) = FloatP(m_yon) / safeCos;

        enoki::masked(ray->time, active) = time;
        if (weight)
            enoki::masked(*weight, active) = FloatP(1.f);

        ray->UpdateReciprocalDirection();
    }

    void PerspectiveCamera::SampleRay(const FloatP &uPixelX, const FloatP &uPixelY,
                                      const FloatP &time, RayP *ray, FloatP *pdf,
                                      FloatP *weight, MaskP active) const
    {
        const FloatP x = uPixelX * FloatP(float(m_xRes));
        const FloatP y = uPixelY * FloatP(float(m_yRes));
        GenerateRay(x, y, time, ray, weight, active);
        enoki::masked(*pdf, active) = Pdf(*ray, active);
    }

    FloatP PerspectiveCamera::Pdf(const RayP &ray, MaskP active) const
    {
        // Endpoint density.
        const Vector3fP d = normalize(ray.d);
        const FloatP cosT = dot(d, m_normal);
        const MaskP valid = active && (cosT > FloatP(EPS_DENOM));
        const FloatP cos2 = cosT * cosT;
        const FloatP pdf = FloatP(1.f) / (FloatP(m_apixel) * cos2 * cosT);
        return select(valid, pdf, FloatP(0.f));
    }

    std::shared_ptr<Camera> PerspectiveCamera::CreateCamera(const PluginContext &ctx)
    {
        // Resolution comes from the film's ParamSet.
        int xRes = 800, yRes = 600;
        if (ctx.filmParams)
        {
            xRes = ctx.filmParams->FindOneInt("xresolution", 800);
            yRes = ctx.filmParams->FindOneInt("yresolution", 600);
        }

        const float hither = std::max(EPS_RAY,
                                      ctx.params ? ctx.params->FindOneFloat("hither", 1e-3f) : 1e-3f);
        float yon = ctx.params ? ctx.params->FindOneFloat("yon", 1e30f) : 1e30f;
        yon = std::max(hither, yon);

        const float shutterOpen =
            ctx.params ? ctx.params->FindOneFloat("shutteropen", 0.f) : 0.f;
        const float fov = ctx.params ? ctx.params->FindOneFloat("fov", 90.f) : 90.f;

        // Frame aspect ratio -> default screen window.
        const float frame = ctx.params
                                ? ctx.params->FindOneFloat("frameaspectratio",
                                                           float(xRes) / float(yRes))
                                : float(xRes) / float(yRes);
        float screen[4];
        if (frame > 1.f)
        {
            screen[0] = -frame;
            screen[1] = frame;
            screen[2] = -1.f;
            screen[3] = 1.f;
        }
        else
        {
            screen[0] = -1.f;
            screen[1] = 1.f;
            screen[2] = -1.f / frame;
            screen[3] = 1.f / frame;
        }
        std::uint32_t swi = 0;
        const float *sw = ctx.params ? ctx.params->FindFloat("screenwindow", &swi)
                                     : nullptr;
        if (sw && swi == 4)
            for (int i = 0; i < 4; ++i)
                screen[i] = sw[i];

        return std::make_shared<PerspectiveCamera>(
            ctx.transform, screen, hither, yon, shutterOpen, xRes, yRes, fov);
    }

    LUX2_REGISTER_CAMERA(PerspectiveCamera, "perspective");

} // namespace lux2
