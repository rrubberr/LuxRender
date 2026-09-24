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
#include "core/transform.h"

#include <memory>

namespace lux2
{

    struct PluginContext;

    class PerspectiveCamera : public Camera
    {
    public:
        // screen[4] = {x0, x1, y0, y1} film plane window.
        PerspectiveCamera(const Transform &cameraToWorld, const float screen[4],
                          float hither, float yon, float shutterOpen,
                          int xRes, int yRes, float fovDeg);

        void GenerateRay(const FloatP &x, const FloatP &y, const FloatP &time,
                         RayP *ray, FloatP *weight,
                         MaskP active = MaskP(true)) const override;

        void SampleRay(const FloatP &uPixelX, const FloatP &uPixelY,
                       const FloatP &time, RayP *ray, FloatP *pdf, FloatP *weight,
                       MaskP active = MaskP(true)) const override;

        FloatP Pdf(const RayP &ray, MaskP active = MaskP(true)) const override;

        int PixelWidth() const override { return m_xRes; }
        int PixelHeight() const override { return m_yRes; }

        static std::shared_ptr<Camera> CreateCamera(const PluginContext &ctx);

    private:
        Transform m_cameraToWorld;
        Point3fP m_pos;     // camera origin
        Vector3fP m_normal; // forward view direction
        // Film-plane extent at z == 1, from screen window * scale.
        float m_screenX0, m_screenX1, m_screenY0, m_screenY1;
        float m_tanHalf; // tan(fov/2), film plane at z == 1
        float m_apixel;  // physical pixel area
        float m_hither, m_yon;
        float m_shutterOpen;
        int m_xRes, m_yRes;
    };

} // namespace lux2

#endif // LUX2_PERSPECTIVE_H
