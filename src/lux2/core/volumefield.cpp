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

#include "core/volumefield.h"

#include <enoki/array.h>

#include <cassert>

namespace lux2
{
    // ---------------------------------------------------------------------------
    // Majorant DDA
    // ---------------------------------------------------------------------------

    void MajorantDDA::Advance(const MajorantGrid &grid, MaskP active)
    {
        // Advance to the next cell along the dominant axis.
        static_cast<void>(grid);
        static_cast<void>(active);
        assert(false && "MajorantDDA::Advance not implemented (Stage 5)");
    }

    void DDAInit(MajorantDDA &dda, const MajorantGrid &grid,
                 const Point3fP &p0, const Vector3fP &d,
                 const FloatP &tStart, MaskP active)
    {
        // Initialize voxel walk at p0 + d*tStart.
        static_cast<void>(dda);
        static_cast<void>(grid);
        static_cast<void>(p0);
        static_cast<void>(d);
        static_cast<void>(tStart);
        static_cast<void>(active);
        assert(false && "DDAInit not implemented (Stage 5)");
    }

    FloatP GridField::Eval(const Point3fP &pField, MaskP active) const
    {
        // Trilinear interpolation.
        static_cast<void>(pField);
        static_cast<void>(active);
        assert(false && "GridField::Eval not implemented (Stage 6)");
        return FloatP(0.f);
    }

    void GridField::BuildMajorantGrid(MajorantGrid &grid, int res) const
    {
        // Coarse max density per cell.
        static_cast<void>(grid);
        static_cast<void>(res);
        assert(false && "GridField::BuildMajorantGrid not implemented (Stage 6)");
    }

    // ---------------------------------------------------------------------------
    // ExponentialField
    // ---------------------------------------------------------------------------

    void ExponentialField::BuildMajorantGrid(MajorantGrid &grid, int res) const
    {
        // Defer to a single global max cell.
        static_cast<void>(res);
        grid.nx = grid.ny = grid.nz = 1;
        grid.maxDensity.assign(1, m_maxDensity);
        grid.bound = m_extent;
        const Vector3f diag = m_extent.Diagonal();
        grid.cellSize = diag;
        grid.invCellSize = Vector3f(
            diag.x() > 0.f ? 1.f / diag.x() : 0.f,
            diag.y() > 0.f ? 1.f / diag.y() : 0.f,
            diag.z() > 0.f ? 1.f / diag.z() : 0.f);
    }

} // namespace lux2
