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

#ifndef LUX2_PERSPECTIVE_H
#define LUX2_PERSPECTIVE_H

#include "core/camera.h"
#include "core/math/transform.h"

#include <memory>

namespace lux2
{

    struct PluginContext;

    class PerspectiveCamera : public Camera
    {
    public:
        // screen[4] = {x0, x1, y0, y1} film plane window.
        // lensRadius == 0 is a pinhole. blades < 3 selects a circular aperture.
        PerspectiveCamera(const Transform &cameraToWorld, const float screen[4],
                          float hither, float yon, float shutterOpen,
                          int xRes, int yRes, float fovDeg,
                          float lensRadius = 0.f, float focalDistance = 1e30f,
                          int blades = 0, int distribution = 0, int power = 3,
                          bool autoFocus = false);

        void GenerateRay(const FloatP &x, const FloatP &y, const FloatP &time,
                         RayP *ray, FloatP *weight,
                         MaskP active = MaskP(true),
                         const FloatP &uLensX = FloatP(0.f),
                         const FloatP &uLensY = FloatP(0.f)) const override;

        void SampleRay(const FloatP &uPixelX, const FloatP &uPixelY,
                       const FloatP &time, RayP *ray, FloatP *pdf, FloatP *weight,
                       MaskP active = MaskP(true),
                       const FloatP &uLensX = FloatP(0.f),
                       const FloatP &uLensY = FloatP(0.f)) const override;

        FloatP Pdf(const RayP &ray, MaskP active = MaskP(true)) const override;

        MaskP Project(const Vector3fP &dirWorld, FloatP *x, FloatP *y,
                      FloatP *We, FloatP *cosCam,
                      MaskP active, const Point3fP &origin) const override;

        int SampleDimensionCount() const override
        {
            return m_lensRadius > 0.f ? 2 : 0;
        }

        void AutoFocus(const EmbreeScene *scene) override;

        int PixelWidth() const override { return m_xRes; }
        int PixelHeight() const override { return m_yRes; }

        bool OriginPoint(Point3f *out) const override;

        static std::shared_ptr<Camera> CreateCamera(const PluginContext &ctx);

    private:
        Transform m_cameraToWorld;
        Transform m_worldToCamera; // inverse
        Point3fP m_pos;     // camera origin
        Vector3fP m_normal; // forward view direction
        // Film-plane extent at z == 1, from screen window * scale.
        float m_screenX0, m_screenX1, m_screenY0, m_screenY1;
        float m_tanHalf; // tan(fov/2), film plane at z == 1
        float m_apixel;  // physical pixel area
        float m_hither, m_yon;
        float m_shutterOpen;
        int m_xRes, m_yRes;

        // Thin-lens depth of field. lensRadius == 0 reduces to a pinhole.
        float m_lensRadius;    // aperture radius (camera-space units)
        float m_focalDistance; // focus plane distance along the view axis
        int m_apertureShape;   // blades (<3 = circular)
        int m_distribution;    // radial profile selector (parity only)
        int m_power;           // exponent for exponential profiles
        bool m_wantsAutoFocus;
    };

} // namespace lux2

#endif // LUX2_PERSPECTIVE_H
