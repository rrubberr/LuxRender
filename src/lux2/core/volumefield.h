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

#ifndef LUX2_VOLUMEFIELD_H
#define LUX2_VOLUMEFIELD_H

#include "core/math/vecp.h"
#include "core/math/geometry.h"
#include "core/math/bbox.h"
#include "core/math/math.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace lux2
{

    // ---------------------------------------------------------------------------
    // MajorantGrid
    // ---------------------------------------------------------------------------

    // A 3D grid of maximum density values.
    struct MajorantGrid
    {
        int nx = 0, ny = 0, nz = 0;
        std::vector<float> maxDensity; // [z*ny*nx + y*nx + x], z-slowest

        // World space bounding box of the field.
        BBox bound;

        // Cell size in each axis.
        Vector3f cellSize;
        Vector3f invCellSize; // 1/cellSize

        bool IsValid() const { return nx > 0 && ny > 0 && nz > 0; }

        float Get(int x, int y, int z) const
        {
            return maxDensity[z * ny * nx + y * nx + x];
        }

        // Global maximum across all cells.
        float GlobalMax() const
        {
            if (maxDensity.empty())
                return 0.f;
            return *std::max_element(maxDensity.begin(), maxDensity.end());
        }
    };

    // ---------------------------------------------------------------------------
    // MajorantDDA
    // ---------------------------------------------------------------------------

    // Stateful traversal structure.
    struct MajorantDDA
    {
        // Current voxel indices per lane.
        Int32P ix, iy, iz;

        // Step direction (+1 or -1) per axis.
        Int32P stepX, stepY, stepZ;

        // Distance to next cell boundary along each axis.
        FloatP tMaxX, tMaxY, tMaxZ;

        // Distance to traverse one full cell along each axis.
        FloatP tDeltaX, tDeltaY, tDeltaZ;

        // Current cell's majorant.
        FloatP majorant;

        // True while the lane is still inside the grid.
        MaskP inside;

        // Advance to the next cell along the dominant axis.
        void Advance(const MajorantGrid &grid, MaskP active);
    };

    // Initialize a DDA for a ray segment in field space.
    void DDAInit(MajorantDDA &dda, const MajorantGrid &grid,
                 const Point3fP &p0, const Vector3fP &d,
                 const FloatP &tStart, MaskP active);

    // ---------------------------------------------------------------------------
    // VolumeField abstract interface
    // ---------------------------------------------------------------------------

    class VolumeField
    {
    public:
        virtual ~VolumeField() = default;

        // Evaluate the scalar density at a fieldspace point.
        virtual FloatP Eval(const Point3fP &pField, MaskP active) const = 0;

        // Global upper bound on density.
        virtual float Max() const = 0;

        // Build a majorant grid at the given resolution.
        virtual void BuildMajorantGrid(MajorantGrid &grid, int res) const = 0;
    };

    // ---------------------------------------------------------------------------
    // ConstantField
    // ---------------------------------------------------------------------------

    class ConstantField : public VolumeField
    {
    public:
        explicit ConstantField(float value) : m_value(value) {}

        FloatP Eval(const Point3fP &, MaskP) const override
        {
            return FloatP(m_value);
        }
        float Max() const override { return m_value; }

        void BuildMajorantGrid(MajorantGrid &grid, int res) const override
        {
            grid.nx = grid.ny = grid.nz = 1;
            grid.maxDensity.assign(1, m_value);
            grid.bound = BBox(Point3f(-1.f), Point3f(1.f));
            grid.cellSize = grid.bound.Diagonal();
            grid.invCellSize = Vector3f(1.f / grid.cellSize.x(),
                                        1.f / grid.cellSize.y(),
                                        1.f / grid.cellSize.z());
        }

    private:
        float m_value;
    };

    // ---------------------------------------------------------------------------
    // ExponentialField
    // ---------------------------------------------------------------------------

    // Operates in fieldspace.
    class ExponentialField : public VolumeField
    {
    public:
        ExponentialField(float scale, float decay, const Point3f &base,
                         const Vector3f &dir, const BBox &extent)
            : m_scale(scale), m_decay(decay), m_base(base),
              m_dir(enoki::normalize(dir)), m_extent(extent)
        {
            // Max density is at the lowest point along dir within extent.
            float minH = std::numeric_limits<float>::infinity();
            for (int i = 0; i < 8; ++i)
            {
                Point3f c(
                    (i & 1) ? m_extent.pMax.x() : m_extent.pMin.x(),
                    (i & 2) ? m_extent.pMax.y() : m_extent.pMin.y(),
                    (i & 4) ? m_extent.pMax.z() : m_extent.pMin.z());
                float h = (c - m_base).x() * m_dir.x() +
                          (c - m_base).y() * m_dir.y() +
                          (c - m_base).z() * m_dir.z();
                minH = std::min(minH, h);
            }
            m_maxDensity = m_scale * std::exp(-m_decay * minH);
        }

        FloatP Eval(const Point3fP &pField, MaskP active) const override
        {
            Vector3fP dp = pField - Point3fP(m_base);
            FloatP h = dp.x() * FloatP(m_dir.x()) +
                       dp.y() * FloatP(m_dir.y()) +
                       dp.z() * FloatP(m_dir.z());
            FloatP d = m_scale * enoki::exp(-m_decay * h);
            // Zero outside extent.
            MaskP inExtent = (pField.x() >= FloatP(m_extent.pMin.x())) &&
                             (pField.x() <= FloatP(m_extent.pMax.x())) &&
                             (pField.y() >= FloatP(m_extent.pMin.y())) &&
                             (pField.y() <= FloatP(m_extent.pMax.y())) &&
                             (pField.z() >= FloatP(m_extent.pMin.z())) &&
                             (pField.z() <= FloatP(m_extent.pMax.z()));
            return select(active && inExtent, d, FloatP(0.f));
        }

        float Max() const override { return m_maxDensity; }

        void BuildMajorantGrid(MajorantGrid &grid, int res) const override;

    private:
        float m_scale, m_decay;
        Point3f m_base;
        Vector3f m_dir;
        BBox m_extent;
        float m_maxDensity;
    };

    // ---------------------------------------------------------------------------
    // GridField
    // ---------------------------------------------------------------------------

    // Volumegrid density evaluation:
    class GridField : public VolumeField
    {
    public:
        enum WrapMode
        {
            WRAP_REPEAT,
            WRAP_BLACK,
            WRAP_WHITE,
            WRAP_CLAMP
        };

        GridField(int nx, int ny, int nz, const float *data,
                  const BBox &extent, WrapMode wrap = WRAP_BLACK)
            : m_nx(nx), m_ny(ny), m_nz(nz), m_extent(extent), m_wrap(wrap)
        {
            m_density.assign(data, data + nx * ny * nz);
            // Compute global max for the majorant bound.
            m_maxVal = 0.f;
            for (float v : m_density)
                m_maxVal = std::max(m_maxVal, v);
        }

        FloatP Eval(const Point3fP &pField, MaskP active) const override;
        float Max() const override { return m_maxVal; }

        void BuildMajorantGrid(MajorantGrid &grid, int res) const override;

        int Nx() const { return m_nx; }
        int Ny() const { return m_ny; }
        int Nz() const { return m_nz; }
        const std::vector<float> &Data() const { return m_density; }
        const BBox &Extent() const { return m_extent; }

    private:
        float D(int x, int y, int z) const
        {
            x = ClampF(x, 0, m_nx - 1);
            y = ClampF(y, 0, m_ny - 1);
            z = ClampF(z, 0, m_nz - 1);
            return m_density[z * m_nx * m_ny + y * m_nx + x];
        }

        int m_nx, m_ny, m_nz;
        std::vector<float> m_density;
        BBox m_extent;
        WrapMode m_wrap;
        float m_maxVal = 0.f;
    };

} // namespace lux2

#endif // LUX2_VOLUMEFIELD_H
