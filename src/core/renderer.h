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

#ifndef LUX2_RENDERER_H
#define LUX2_RENDERER_H

#include "core/integrator.h"

namespace lux2
{

    class Scene;

    // Abstract renderer.
    class Renderer
    {
    public:
        virtual ~Renderer() = default;

        // Render the scene with the given surface integrator.
        virtual void Render(const Scene &scene, SurfaceIntegrator &integrator) = 0;

        // Cooperative render control.
        virtual void Pause() {}
        virtual void Resume() {}
        virtual void Terminate() {}
        virtual bool IsRendering() const { return false; }
    };

} // namespace lux2

#endif // LUX2_RENDERER_H
