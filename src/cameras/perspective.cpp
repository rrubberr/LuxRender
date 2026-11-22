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

#include "core/accel/embree2.h"
#include "core/dynload.h"
#include "core/error.h"
#include "core/math/math.h"
#include "core/paramset.h"
#include "core/register.h"
#include "core/sampling.h"

#include <algorithm>
#include <cmath>

namespace lux2
{
    namespace
    {
        // Regular ngon of circumradius r.
        float polygonArea(int blades, float r)
        {
            const float a = PI / float(blades);
            return float(blades) * r * r * std::sin(a) * std::cos(a);
        }

        // Inside the aperture of circumradius r. Disk for blades < 3, else the
        // intersection of the n edge half-planes (dot(p, normal_i) <= apothem).
        MaskP InsideAperture(const Point3fP &p, int blades, float r)
        {
            if (blades < 3)
                return fmadd(p.x(), p.x(), p.y() * p.y()) <= FloatP(r * r);
            const float apothem = r * std::cos(PI / float(blades));
            MaskP inside(true);
            for (int i = 0; i < blades; ++i)
            {
                const float a = PI * (2.f * float(i) + 1.f) / float(blades);
                const Vector3fP n(std::cos(a), std::sin(a), FloatP(0.f));
                inside = inside &&
                         (dot(Vector3fP(p.x(), p.y(), FloatP(0.f)), n) <=
                          FloatP(apothem));
            }
            return inside;
        }
    } // namespace

    bool PerspectiveCamera::OriginPoint(Point3f *out) const
    {
        alignas(64) float x[PACKET_WIDTH], y[PACKET_WIDTH], z[PACKET_WIDTH];
        enoki::store_unaligned(x, m_pos.x());
        enoki::store_unaligned(y, m_pos.y());
        enoki::store_unaligned(z, m_pos.z());
        *out = Point3f(x[0], y[0], z[0]);
        return true;
    }

    PerspectiveCamera::PerspectiveCamera(const Transform &cameraToWorld,
                                         const float screen[4], float hither,
                                         float yon, float shutterOpen,
                                         int xRes, int yRes, float fovDeg,
                                         float lensRadius, float focalDistance,
                                         int blades, int distribution, int power,
                                         bool autoFocus)
        : m_cameraToWorld(cameraToWorld),
          m_worldToCamera(cameraToWorld.inverse()),
          m_hither(hither), m_yon(yon), m_shutterOpen(shutterOpen),
          m_xRes(xRes), m_yRes(yRes), m_lensRadius(lensRadius),
          m_focalDistance(focalDistance), m_apertureShape(blades),
          m_distribution(distribution), m_power(power),
          m_wantsAutoFocus(autoFocus)
    {
        (void)m_distribution; // radial profiles are uniform-only for now
        (void)m_power;
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
                                        FloatP *weight, MaskP active,
                                        const FloatP &uLensX,
                                        const FloatP &uLensY) const
    {
        // Raster -> film-plane screen coords.
        const FloatP fx = x / FloatP(float(m_xRes));
        const FloatP fy = y / FloatP(float(m_yRes));
        const FloatP screenX = m_screenX0 + (m_screenX1 - m_screenX0) * fx;
        const FloatP screenY = m_screenY1 + (m_screenY0 - m_screenY1) * fy;

        // Camera space direction to the film plane at z == 1.
        const FloatP tanHalf(m_tanHalf);
        const Vector3fP dirCam(screenX * tanHalf, screenY * tanHalf, FloatP(1.f));

        Point3fP originWorld;
        Vector3fP dirWorld;
        if (m_lensRadius > 0.f)
        {
            // Map raw uniforms onto the aperture disk/polygon.
            FloatP lx, ly;
            sampleLens(uLensX, uLensY, m_apertureShape, m_distribution,
                       m_power, &lx, &ly);
            const Vector3fP pLens(lx * FloatP(m_lensRadius),
                                  ly * FloatP(m_lensRadius), FloatP(0.f));
            // Converge the pinhole ray toward the focal plane.
            // (pFocus - pLens)/f == dirCam - pLens/f since dirCam.z == 1.
            const FloatP invF(1.f / m_focalDistance);
            originWorld =
                m_cameraToWorld * Point3fP(pLens.x(), pLens.y(), FloatP(0.f));
            dirWorld = normalize(m_cameraToWorld * (dirCam - pLens * invF));
        }
        else
        {
            originWorld = m_pos;
            dirWorld = normalize(m_cameraToWorld * dirCam);
        }

        enoki::masked(ray->o, active) = originWorld;
        enoki::masked(ray->d, active) = dirWorld;

        // Clip along the view axis t such that (t * dirWorld).z_cam == hither/yon.
        // Both the pinhole and the lens plane sit at z_cam == 0.
        const FloatP cosT = dot(dirWorld, m_normal);
        const FloatP safeCos = select(cosT > FloatP(EPS_DENOM), cosT, FloatP(EPS_DENOM));
        enoki::masked(ray->mint, active) = FloatP(m_hither) / safeCos;
        enoki::masked(ray->maxt, active) = FloatP(m_yon) / safeCos;

        enoki::masked(ray->time, active) = time;
        if (weight)
            enoki::masked(*weight, active) = FloatP(1.f);
    }

    void PerspectiveCamera::SampleRay(const FloatP &uPixelX, const FloatP &uPixelY,
                                      const FloatP &time, RayP *ray, FloatP *pdf,
                                      FloatP *weight, MaskP active,
                                      const FloatP &uLensX,
                                      const FloatP &uLensY) const
    {
        const FloatP x = uPixelX * FloatP(float(m_xRes));
        const FloatP y = uPixelY * FloatP(float(m_yRes));
        GenerateRay(x, y, time, ray, weight, active, uLensX, uLensY);
        enoki::masked(*pdf, active) = Pdf(*ray, active);
    }

    FloatP PerspectiveCamera::Pdf(const RayP &ray, MaskP active) const
    {
        // Solid angle density of the primary ray.
        const Vector3fP d = normalize(ray.d);
        const FloatP cosT = dot(d, m_normal);
        MaskP valid = active && (cosT > FloatP(EPS_DENOM));

        if (m_lensRadius > 0.f)
        {
            // A lens ray is generatable only if its aperture hit lies inside
            // the lens and its focus point projects inside the film window.
            const Point3fP oCam = m_worldToCamera * ray.o;
            const Vector3fP dCam = normalize(m_worldToCamera * ray.d);
            const FloatP tLens = -oCam.z() / max(dCam.z(), FloatP(EPS_DENOM));
            const Point3fP pLens = oCam + dCam * tLens;
            valid = valid && InsideAperture(pLens, m_apertureShape, m_lensRadius);

            const FloatP f(m_focalDistance);
            const FloatP tFocus = (f - oCam.z()) / max(dCam.z(), FloatP(EPS_DENOM));
            const Point3fP pFocus = oCam + dCam * tFocus;
            const FloatP tanHalf(m_tanHalf);
            const FloatP sx = pFocus.x() / (tanHalf * f);
            const FloatP sy = pFocus.y() / (tanHalf * f);
            const FloatP fx = (sx - FloatP(m_screenX0)) /
                              FloatP(m_screenX1 - m_screenX0) *
                              FloatP(float(m_xRes));
            const FloatP fy = (sy - FloatP(m_screenY1)) /
                              FloatP(m_screenY0 - m_screenY1) *
                              FloatP(float(m_yRes));
            valid = valid && (fx >= FloatP(0.f)) && (fx < FloatP(float(m_xRes))) &&
                    (fy >= FloatP(0.f)) && (fy < FloatP(float(m_yRes)));
        }

        const FloatP cos2 = cosT * cosT;
        const FloatP pdf = FloatP(1.f) / (FloatP(m_apixel) * cos2 * cosT);
        return select(valid, pdf, FloatP(0.f));
    }

    MaskP PerspectiveCamera::Project(const Vector3fP &dirWorld, FloatP *x,
                                     FloatP *y, FloatP *We, FloatP *cosCam,
                                     MaskP active, const Point3fP &origin) const
    {
        const Vector3fP d = normalize(m_worldToCamera * dirWorld);
        const FloatP cosT = d.z();
        const FloatP tanHalf(m_tanHalf);
        MaskP valid = active && (cosT > FloatP(EPS_DENOM));

        FloatP sx, sy;
        if (m_lensRadius > 0.f)
        {
            // The camera vertex is the lens point.
            const Point3fP oCam = m_worldToCamera * origin;
            const FloatP tLens = -oCam.z() / max(d.z(), FloatP(EPS_DENOM));
            const Point3fP pLens = oCam + d * tLens;
            valid = valid && InsideAperture(pLens, m_apertureShape, m_lensRadius);

            const FloatP f(m_focalDistance);
            const FloatP tFocus = (f - oCam.z()) / max(d.z(), FloatP(EPS_DENOM));
            const Point3fP pFocus = oCam + d * tFocus;
            sx = pFocus.x() / (tanHalf * f);
            sy = pFocus.y() / (tanHalf * f);
        }
        else
        {
            sx = d.x() / (cosT * tanHalf);
            sy = d.y() / (cosT * tanHalf);
        }
        const FloatP fx = (sx - FloatP(m_screenX0)) /
                          FloatP(m_screenX1 - m_screenX0);
        const FloatP fy = (sy - FloatP(m_screenY1)) /
                          FloatP(m_screenY0 - m_screenY1);
        enoki::masked(*x, active) = fx * FloatP(float(m_xRes));
        enoki::masked(*y, active) = fy * FloatP(float(m_yRes));
        enoki::masked(*cosCam, active) = cosT;
        const FloatP cos2 = cosT * cosT;
        enoki::masked(*We, active) =
            FloatP(1.f) / (FloatP(m_apixel) * cos2 * cosT);

        // `valid` already carries the forward test and, for lens cameras, the
        // aperture containment. Add the film-window bounds.
        return valid && (*x >= FloatP(0.f)) && (*x < FloatP(float(m_xRes))) &&
               (*y >= FloatP(0.f)) && (*y < FloatP(float(m_yRes)));
    }

    void PerspectiveCamera::AutoFocus(const EmbreeScene *scene)
    {
        if (!m_wantsAutoFocus || !scene)
            return;

        // Trace the center view ray and read the hit distance.
        const FloatP sx = FloatP(0.5f * (m_screenX0 + m_screenX1));
        const FloatP sy = FloatP(0.5f * (m_screenY0 + m_screenY1));
        const Vector3fP dirCam(sx * FloatP(m_tanHalf), sy * FloatP(m_tanHalf),
                               FloatP(1.f));
        RayP ray;
        enoki::masked(ray.o, MaskP(true)) = m_pos;
        enoki::masked(ray.d, MaskP(true)) = normalize(m_cameraToWorld * dirCam);
        ray.mint = FloatP(m_hither);
        ray.maxt = FloatP(m_yon);

        HitP hit;
        scene->Intersect(ray, hit, Coherent::Yes);
        if (hit.hit[0])
        {
            // hit.t is distance along the ray; m_focalDistance is measured
            // along the view axis.
            const Vector3fP dCam = normalize(m_worldToCamera * ray.d);
            const FloatP axial = hit.t[0] * dCam.z()[0];
            m_focalDistance = float(axial[0]);
        }
        else
        {
            LOG(LUX_WARNING, LUX_NOERROR)
                << "Autofocus found no surface; keeping focaldistance.";
        }
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

        const ParamSet &params = ctx.Params();

        const float hither =
            std::max(EPS_RAY, params.FindOneFloat("hither", 1e-3f));
        float yon = params.FindOneFloat("yon", 1e30f);
        yon = std::max(hither, yon);

        const float shutterOpen = params.FindOneFloat("shutteropen", 0.f);
        const float fov = params.FindOneFloat("fov", 90.f);

        // Frame aspect ratio -> default screen window.
        const float frame = params.FindOneFloat("frameaspectratio",
                                                float(xRes) / float(yRes));
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
        const float *sw = params.FindFloat("screenwindow", &swi);
        if (sw && swi == 4)
            for (int i = 0; i < 4; ++i)
                screen[i] = sw[i];

        // Thin lens depth of field.
        const float lensRadius =
            std::max(0.f, params.FindOneFloat("lensradius", 0.f));
        const float focalDistance =
            params.FindOneFloat("focaldistance", 1e30f);
        const bool autoFocus = params.FindOneBool("autofocus", false);
        const int blades = params.FindOneInt("blades", 0);
        const std::string dist =
            params.FindOneString("distribution", "uniform");
        int distribution = 0;
        if (dist == "exponential")
            distribution = 1;
        else if (dist == "inverse exponential")
            distribution = 2;
        else if (dist == "gaussian")
            distribution = 3;
        else if (dist == "inverse gaussian")
            distribution = 4;
        else if (dist == "triangular")
            distribution = 5;
        const int power = params.FindOneInt("power", 3);

        return std::make_shared<PerspectiveCamera>(
            ctx.transform, screen, hither, yon, shutterOpen, xRes, yRes, fov,
            lensRadius, focalDistance, blades, distribution, power, autoFocus);
    }

    LUX2_REGISTER_CAMERA(PerspectiveCamera, "perspective");

} // namespace lux2
