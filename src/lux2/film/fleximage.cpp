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

#include "film/fleximage.h"

#include "core/color.h"
#include "core/colorsystem.h"
#include "core/dynload.h"
#include "core/exrio.h"
#include "core/math.h"
#include "core/paramset.h"
#include "core/pngio.h"
#include "core/register.h"
#include "core/tonemap.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <mutex>

namespace lux2
{

    namespace
    {
        // Crop window math.
        int CeilToInt(float v) { return int(std::ceil(v)); }

        // Round n up to a multiple of m (m > 0).
        int RoundUp(int n, int m) { return ((n + m - 1) / m) * m; }
    } // namespace

    FlexImageFilm::FlexImageFilm(int xres, int yres, const Filter *filter,
                                 const float crop[4],
                                 const std::string &filename,
                                 bool premultiplyAlpha)
        : m_xres(xres), m_yres(yres), m_filename(filename),
          m_filter(filter), m_premultiplyAlpha(premultiplyAlpha)
    {
        // Crop window in pixels.
        m_xStart = CeilToInt(float(xres) * crop[0]);
        m_xCount = std::max(1, CeilToInt(float(xres) * crop[1]) - m_xStart);
        m_yStart = CeilToInt(float(yres) * crop[2]);
        m_yCount = std::max(1, CeilToInt(float(yres) * crop[3]) - m_yStart);

        const size_t n = size_t(m_xCount) * size_t(m_yCount);
        m_bX.assign(n, 0.f);
        m_bY.assign(n, 0.f);
        m_bZ.assign(n, 0.f);
        m_bAlpha.assign(n, 0.f);
        m_bW.assign(n, 0.f);
    }

    // Private constructor.
    FlexImageFilm::FlexImageFilm(int xres, int yres, const Filter *filter,
                                 int xStart, int xCount, int yStart, int yCount,
                                 bool premultiplyAlpha)
        : m_xres(xres), m_yres(yres), m_xStart(xStart), m_xCount(xCount),
          m_yStart(yStart), m_yCount(yCount), m_filter(filter),
          m_premultiplyAlpha(premultiplyAlpha)
    {
        const size_t n = size_t(m_xCount) * size_t(m_yCount);
        m_bX.assign(n, 0.f);
        m_bY.assign(n, 0.f);
        m_bZ.assign(n, 0.f);
        m_bAlpha.assign(n, 0.f);
        m_bW.assign(n, 0.f);
    }

    // Create a private accumulation buffer.
    FlexImageFilm::FlexImageFilm(int xres, int yres, const Filter *filter,
                                 int xStart, int xCount, int yStart, int yCount,
                                 bool premultiplyAlpha, ScratchTag)
        : FlexImageFilm(xres, yres, filter, xStart, xCount, yStart, yCount,
                        premultiplyAlpha)
    {
    }

    // -----------------------------------------------------------------------
    // Continuous work-stealing scheduler interface
    // -----------------------------------------------------------------------

    int FlexImageFilm::SetupTiles(int tileSize, int haloX, int haloY)
    {
        // Tile dims rounded up to a multiple of Enoki PACKET_WIDTH so
        // SIMD writes don't cross tile edges.
        const int pw = int(PACKET_WIDTH);
        const int tw = std::max(pw, RoundUp(tileSize > 0 ? tileSize : m_xCount, pw));
        const int th = std::max(pw, RoundUp(tileSize > 0 ? tileSize : m_yCount, pw));

        m_tiles.clear();
        m_overlaps.clear();
        m_tileCols = 0;
        m_tileRows = 0;
        m_maxScratchArea = 0;
        if (m_xCount <= 0 || m_yCount <= 0)
            return 0;

        const int cropX1 = m_xStart + m_xCount, cropY1 = m_yStart + m_yCount;
        for (int y = m_yStart; y < cropY1; y += th)
        {
            const int y1 = std::min(y + th, cropY1);
            ++m_tileRows;
            m_tileCols = 0; // recomputed per row below; all rows share the split
            for (int x = m_xStart; x < cropX1; x += tw)
            {
                const int x1 = std::min(x + tw, cropX1);
                FilmTile t;
                t.x0 = x;
                t.y0 = y;
                t.x1 = x1;
                t.y1 = y1;
                m_tiles.push_back(t);
                ++m_tileCols;
            }
        }

        // One mutex and one sample counter per tile.
        m_tileMutexes.clear();
        m_tileMutexes.reserve(m_tiles.size());
        for (size_t i = 0; i < m_tiles.size(); ++i)
            m_tileMutexes.push_back(std::make_unique<std::mutex>());
        // Initialize each atomic to 0.
        m_tileSampleCount = std::vector<std::atomic<double>>(m_tiles.size());

        // Scratch window domain grown by the filter halo, clamped
        // to the crop window. Record the largest area so I only
        // reserve once for every tile.
        for (FilmTile &t : m_tiles)
        {
            t.sx0 = std::max(t.x0 - haloX, m_xStart);
            t.sy0 = std::max(t.y0 - haloY, m_yStart);
            t.sx1 = std::min(t.x1 + haloX, cropX1);
            t.sy1 = std::min(t.y1 + haloY, cropY1);
            m_maxScratchArea = std::max(
                m_maxScratchArea, size_t(t.sx1 - t.sx0) * size_t(t.sy1 - t.sy0));
        }

        // A tile's scratch domain reaches at most ceil(halo/tileDim)
        // grid cells per axis, so only that neighborhood can intersect it.
        const int kx = (haloX + tw - 1) / tw;
        const int ky = (haloY + th - 1) / th;
        for (int r = 0; r < m_tileRows; ++r)
        {
            for (int c = 0; c < m_tileCols; ++c)
            {
                FilmTile &t = m_tiles[size_t(r) * m_tileCols + c];
                t.ovBegin = uint32_t(m_overlaps.size());
                for (int rr = std::max(0, r - ky); rr <= std::min(m_tileRows - 1, r + ky); ++rr)
                {
                    for (int cc = std::max(0, c - kx); cc <= std::min(m_tileCols - 1, c + kx); ++cc)
                    {
                        const uint32_t n = uint32_t(rr * m_tileCols + cc);
                        const FilmTile &nb = m_tiles[n];
                        TileOverlap o;
                        o.tile = n;
                        o.x0 = std::max(t.sx0, nb.x0);
                        o.y0 = std::max(t.sy0, nb.y0);
                        o.x1 = std::min(t.sx1, nb.x1);
                        o.y1 = std::min(t.sy1, nb.y1);
                        if (o.x1 > o.x0 && o.y1 > o.y0)
                            m_overlaps.push_back(o);
                    }
                }
                t.ovCount = uint32_t(m_overlaps.size()) - t.ovBegin;
            }
        }

        ++m_epoch; // invalidates any scratch from the old directory
        return int(m_tiles.size());
    }

    std::unique_ptr<Film> FlexImageFilm::MakeWorkerScratch() const
    {
        // Allocate at the largest scratch window in the directory.
        auto s = std::unique_ptr<FlexImageFilm>(new FlexImageFilm(
            m_xres, m_yres, m_filter, 0, 0, 0, 0, m_premultiplyAlpha,
            ScratchTag{}));
        for (auto *v : {&s->m_bX, &s->m_bY, &s->m_bZ, &s->m_bAlpha, &s->m_bW})
            v->reserve(m_maxScratchArea);
        return s;
    }

    void FlexImageFilm::BindScratch(uint32_t tile, Film &scratch) const
    {
        if (tile >= m_tiles.size())
            return;
        auto &s = static_cast<FlexImageFilm &>(scratch);
        const FilmTile &t = m_tiles[tile];
        s.m_xStart = t.sx0;
        s.m_xCount = t.sx1 - t.sx0;
        s.m_yStart = t.sy0;
        s.m_yCount = t.sy1 - t.sy0;
        const size_t n = size_t(s.m_xCount) * size_t(s.m_yCount);
        // assign() reuses reserved capacity when n <= capacity(): memset only.
        for (auto *v : {&s.m_bX, &s.m_bY, &s.m_bZ, &s.m_bAlpha, &s.m_bW})
            v->assign(n, 0.f);
        s.m_boundTile = tile;
        s.m_boundEpoch = m_epoch;
    }

    FilmTile FlexImageFilm::GetTile(uint32_t index) const
    {
        if (index >= m_tiles.size())
            return FilmTile{0, 0, 0, 0};
        return m_tiles[index];
    }

    void FlexImageFilm::AddRegion(const FlexImageFilm &src,
                                  int x0, int y0, int x1, int y1)
    {
        // The domain is guaranteed inside both films by the overlap directory.
        const int rowLen = x1 - x0;
        for (int y = y0; y < y1; ++y)
        {
            const size_t dBase = size_t(y - m_yStart) * m_xCount +
                                 size_t(x0 - m_xStart);
            const size_t sBase = size_t(y - src.m_yStart) * src.m_xCount +
                                 size_t(x0 - src.m_xStart);
            float *dX = m_bX.data() + dBase;
            const float *sX = src.m_bX.data() + sBase;
            float *dY = m_bY.data() + dBase;
            const float *sY = src.m_bY.data() + sBase;
            float *dZ = m_bZ.data() + dBase;
            const float *sZ = src.m_bZ.data() + sBase;
            float *dA = m_bAlpha.data() + dBase;
            const float *sA = src.m_bAlpha.data() + sBase;
            float *dW = m_bW.data() + dBase;
            const float *sW = src.m_bW.data() + sBase;
            for (int i = 0; i < rowLen; ++i)
            {
                dX[i] += sX[i];
                dY[i] += sY[i];
                dZ[i] += sZ[i];
                dA[i] += sA[i];
                dW[i] += sW[i];
            }
        }
    }

    void FlexImageFilm::MergeScratch(uint32_t tile, const Film &scratch)
    {
        if (tile >= m_tiles.size())
            return;
        const FlexImageFilm *src = dynamic_cast<const FlexImageFilm *>(&scratch);
        if (!src || src == this)
            return;

        // The scratch must be bound to this exact tile under the current
        // directory.
        assert(src->m_boundTile == tile && src->m_boundEpoch == m_epoch);

        // Walk the precomputed overlap list.
        const FilmTile &t = m_tiles[tile];
        for (uint32_t i = 0; i < t.ovCount; ++i)
        {
            const TileOverlap &o = m_overlaps[t.ovBegin + i];
            std::lock_guard<std::mutex> lock(*m_tileMutexes[o.tile]);
            AddRegion(*src, o.x0, o.y0, o.x1, o.y1);
        }
    }

    void FlexImageFilm::AddTileSampleCount(uint32_t tile, double n)
    {
        if (tile < m_tileSampleCount.size())
            m_tileSampleCount[tile].fetch_add(n, std::memory_order_relaxed);
    }

    double FlexImageFilm::SampleCount() const
    {
        if (m_tileSampleCount.empty())
            return m_sampleCount; // flat counter when not partitioned
        double mn = m_tileSampleCount[0].load(std::memory_order_relaxed);
        for (const std::atomic<double> &v : m_tileSampleCount)
            mn = std::min(mn, v.load(std::memory_order_relaxed));
        return mn;
    }

    void FlexImageFilm::Clear()
    {
        std::fill(m_bX.begin(), m_bX.end(), 0.f);
        std::fill(m_bY.begin(), m_bY.end(), 0.f);
        std::fill(m_bZ.begin(), m_bZ.end(), 0.f);
        std::fill(m_bAlpha.begin(), m_bAlpha.end(), 0.f);
        std::fill(m_bW.begin(), m_bW.end(), 0.f);
        m_sampleCount = 0.0;
    }

    void FlexImageFilm::Splat(const FloatP &x, const FloatP &y,
                              const SWCSpectrumP &L,
                              const SpectrumWavelengthsP &sw,
                              const FloatP &alpha, const FloatP &weight,
                              int)
    {
        XYZColorP xyzP = SWCToXYZ(L, sw);

        // Reject infinite or negative Y/alpha/weight.
        const FloatP Y = xyzP[1];
        MaskP active = (Y >= FloatP(0.f)) && enoki::isfinite(Y) &&
                       (alpha >= FloatP(0.f)) && enoki::isfinite(alpha) &&
                       (weight >= FloatP(0.f)) && enoki::isfinite(weight);
        if (!enoki::any(active))
            return;

        // Folds alpha into the XYZ.
        if (m_premultiplyAlpha)
        {
            xyzP[0] = xyzP[0] * alpha;
            xyzP[1] = xyzP[1] * alpha;
            xyzP[2] = xyzP[2] * alpha;
        }

        const float xWidth = m_filter->GetXWidth();
        const float yWidth = m_filter->GetYWidth();

        // Sample center in continuous pixel space.
        const FloatP dx = x - FloatP(0.5f);
        const FloatP dy = y - FloatP(0.5f);

        // Footprint bounds per lane.
        Int32P loX, loY, hiX, hiY;
        loX = enoki::ceil(dx - FloatP(xWidth));
        hiX = enoki::floor(dx + FloatP(xWidth));
        loY = enoki::ceil(dy - FloatP(yWidth));
        hiY = enoki::floor(dy + FloatP(yWidth));

        const int nx = int(std::floor(2.f * xWidth + 1e-4f)) + 1;
        const int ny = int(std::floor(2.f * yWidth + 1e-4f)) + 1;

        // Per-axis weight tables.
        FloatP wx[64], wy[64];
        FloatP totalX(0.f), totalY(0.f);
        for (int xr = 0; xr < nx; ++xr)
        {
            const Int32P xPix = loX + Int32P(xr);
            const FloatP off = enoki::abs(FloatP(xPix) - dx);
            wx[xr] = m_filter->EvaluateXP(off);
            totalX = enoki::select(xPix <= hiX, totalX + wx[xr], totalX);
        }
        for (int yr = 0; yr < ny; ++yr)
        {
            const Int32P yPix = loY + Int32P(yr);
            const FloatP off = enoki::abs(FloatP(yPix) - dy);
            wy[yr] = m_filter->EvaluateYP(off);
            totalY = enoki::select(yPix <= hiY, totalY + wy[yr], totalY);
        }
        const FloatP invTotal =
            enoki::rcp(enoki::max(totalX * totalY, FloatP(1e-20f)));

        const Int32P xStartP(m_xStart), yStartP(m_yStart), xCountP(m_xCount);
        const int maxX = m_xStart + m_xCount - 1;
        const int maxY = m_yStart + m_yCount - 1;
        const Int32P maxXP(maxX), maxYP(maxY);
        const Int32P nIdxP(int(m_bX.size()) - 1);

        float *bufX = m_bX.data();
        float *bufY = m_bY.data();
        float *bufZ = m_bZ.data();
        float *bufA = m_bAlpha.data();
        float *bufW = m_bW.data();

        for (int yr = 0; yr < ny; ++yr)
        {
            const Int32P yPix = loY + Int32P(yr);
            const MaskP rowOk = active && (yPix <= hiY) && (yPix >= yStartP) &&
                                (yPix <= maxYP);
            if (!enoki::any(rowOk))
                continue;
            for (int xr = 0; xr < nx; ++xr)
            {
                const Int32P xPix = loX + Int32P(xr);
                MaskP enabled = rowOk && (xPix <= hiX) && (xPix >= xStartP) &&
                                (xPix <= maxXP);
                if (!enoki::any(enabled))
                    continue;

                const FloatP w = weight * wx[xr] * wy[yr] * invTotal;
                Int32P idx = (yPix - yStartP) * xCountP + (xPix - xStartP);
                // Clamp so masked-off lanes never form out-of-range addresses.
                idx = enoki::clamp(idx, Int32P(0), nIdxP);

                enoki::scatter_add(bufX, w * xyzP[0], idx, enabled);
                enoki::scatter_add(bufY, w * xyzP[1], idx, enabled);
                enoki::scatter_add(bufZ, w * xyzP[2], idx, enabled);
                enoki::scatter_add(bufA, alpha * w, idx, enabled);
                enoki::scatter_add(bufW, w, idx, enabled);
            }
        }
    }

    void FlexImageFilm::Merge(Film *other)
    {
        FlexImageFilm *o = dynamic_cast<FlexImageFilm *>(other);
        if (!o || o == this)
            return;

        // One lock per merge.
        std::lock_guard<std::mutex> lock(m_mergeMutex);

        if (o->m_xCount != m_xCount || o->m_yCount != m_yCount ||
            o->m_xStart != m_xStart || o->m_yStart != m_yStart)
        {
            LOG(LUX_ERROR, LUX_ILLSTATE)
                << "FlexImageFilm::Merge resolution/crop mismatch";
            return;
        }

        const size_t n = m_bX.size();
        for (size_t i = 0; i < n; ++i)
        {
            m_bX[i] += o->m_bX[i];
            m_bY[i] += o->m_bY[i];
            m_bZ[i] += o->m_bZ[i];
            m_bAlpha[i] += o->m_bAlpha[i];
            m_bW[i] += o->m_bW[i];
        }
        m_sampleCount += o->m_sampleCount;
    }

    void FlexImageFilm::GetPixelNormalized(int x, int y, float xyz[3],
                                           float *alpha) const
    {
        xyz[0] = xyz[1] = xyz[2] = 0.f;
        if (alpha)
            *alpha = 0.f;

        const int ix = x - m_xStart;
        const int iy = y - m_yStart;
        if (ix < 0 || ix >= m_xCount || iy < 0 || iy >= m_yCount)
            return;

        const size_t idx = size_t(iy) * m_xCount + size_t(ix);
        const float w = m_bW[idx];
        if (w == 0.f)
            return;

        const float inv = 1.f / w;
        xyz[0] = m_bX[idx] * inv;
        xyz[1] = m_bY[idx] * inv;
        xyz[2] = m_bZ[idx] * inv;
        if (alpha)
            *alpha = m_bAlpha[idx] * inv;
    }

    // -----------------------------------------------------------------------
    // Display pipeline
    // -----------------------------------------------------------------------

    void FlexImageFilm::CreateFrameBuffer()
    {
        const size_t n = size_t(m_xres) * size_t(m_yres);
        if (m_frameBuffer.size() != n * 3)
            m_frameBuffer.assign(n * 3, 0);
        if (m_floatFrameBuffer.size() != n * 3)
            m_floatFrameBuffer.assign(n * 3, 0.f);
        if (m_alphaBuffer.size() != n)
            m_alphaBuffer.assign(n, 0.f);
    }

    unsigned char *FlexImageFilm::GetFrameBuffer()
    {
        CreateFrameBuffer();
        return m_frameBuffer.data();
    }

    float *FlexImageFilm::GetFloatFrameBuffer()
    {
        CreateFrameBuffer();
        return m_floatFrameBuffer.data();
    }

    float *FlexImageFilm::GetAlphaBuffer()
    {
        CreateFrameBuffer();
        return m_alphaBuffer.data();
    }

    std::unique_ptr<ToneMap> FlexImageFilm::BuildToneMap() const
    {
        const char *name = nullptr;
        switch (m_tonemapKernel)
        {
        case TMK_LINEAR:
            name = "linear";
            break;
        case TMK_AUTOLINEAR:
            name = "autolinear";
            break;
        default:
            break;
        }

        if (!name)
        {
            if (!m_tonemapWarned)
            {
                m_tonemapWarned = true;
                LOG(LUX_WARNING, LUX_BADTOKEN)
                    << "Tonemap kernel id " << m_tonemapKernel
                    << " is not implemented in lux2. Using \"autolinear\".";
            }
            name = "autolinear";
        }

        ParamSet ps;
        ps.AddFloat("gamma", &m_linearGamma, 1);
        ps.AddFloat("sensitivity", &m_linearSensitivity, 1);
        ps.AddFloat("exposure", &m_linearExposure, 1);
        ps.AddFloat("fstop", &m_linearFStop, 1);
        return MakeToneMap(name, ps);
    }

    void FlexImageFilm::SnapshotAccum(std::vector<float> &bX,
                                      std::vector<float> &bY,
                                      std::vector<float> &bZ,
                                      std::vector<float> &bAlpha,
                                      std::vector<float> &bW) const
    {
        bX = m_bX;
        bY = m_bY;
        bZ = m_bZ;
        bAlpha = m_bAlpha;
        bW = m_bW;

        // A plain copy above can hold new W with old XYZ. Copy each tile under
        // lock so all five buffers are consistent.
        if (!m_tileMutexes.empty())
        {
            for (size_t t = 0; t < m_tiles.size(); ++t)
            {
                const FilmTile &tl = m_tiles[t];
                std::lock_guard<std::mutex> lock(*m_tileMutexes[t]);
                for (int y = tl.y0; y < tl.y1; ++y)
                {
                    const size_t base = size_t(y - m_yStart) * m_xCount +
                                        size_t(tl.x0 - m_xStart);
                    const size_t len = size_t(tl.x1 - tl.x0);
                    for (size_t i = 0; i < len; ++i)
                    {
                        bX[base + i] = m_bX[base + i];
                        bY[base + i] = m_bY[base + i];
                        bZ[base + i] = m_bZ[base + i];
                        bAlpha[base + i] = m_bAlpha[base + i];
                        bW[base + i] = m_bW[base + i];
                    }
                }
            }
        }
    }

    bool FlexImageFilm::BuildDisplayImage(std::vector<RGBColor> &rgb,
                                          std::vector<float> &alpha,
                                          bool applyTonemap,
                                          const std::vector<float> &bX,
                                          const std::vector<float> &bY,
                                          const std::vector<float> &bZ,
                                          const std::vector<float> &bAlpha,
                                          const std::vector<float> &bW) const
    {
        const size_t nPix = size_t(m_xCount) * size_t(m_yCount);
        rgb.resize(nPix);
        alpha.assign(nPix, 0.f);

        std::vector<XYZColor> xyz(nPix, XYZColor(0.f));
        for (size_t i = 0; i < nPix; ++i)
        {
            const float w = bW[i];
            if (w == 0.f)
                continue;
            const float inv = 1.f / w;
            xyz[i] = XYZColor(bX[i] * inv, bY[i] * inv, bZ[i] * inv);
            alpha[i] = bAlpha[i] * inv;
        }

        // Recover straight color for the tonemapper.
        if (m_premultiplyAlpha)
        {
            for (size_t i = 0; i < nPix; ++i)
            {
                if (alpha[i] > 0.f)
                    xyz[i] /= alpha[i];
            }
        }

        if (applyTonemap)
        {
            std::unique_ptr<ToneMap> tm = BuildToneMap();
            if (tm)
                tm->Map(xyz, m_xCount, m_yCount, 1.f);
        }

        const ColorSystem cs(m_csRed[0], m_csRed[1], m_csGreen[0], m_csGreen[1],
                             m_csBlue[0], m_csBlue[1], m_csWhite[0],
                             m_csWhite[1], 1.f);
        for (size_t i = 0; i < nPix; ++i)
            rgb[i] = cs.ToRGBConstrained(xyz[i]);
        return true;
    }

    void FlexImageFilm::UpdateFrameBuffer()
    {
        CreateFrameBuffer();
        WriteImage(IMAGE_FRAMEBUFFER);
    }

    bool FlexImageFilm::WriteImage(ImageType type)
    {
        if (!(type & (IMAGE_FILEOUTPUT | IMAGE_FRAMEBUFFER)))
            return true;

        // The viewport and the render thread can both request output.
        std::lock_guard<std::mutex> writeLock(m_writeMutex);

        const bool anyFile = (type & IMAGE_FILEOUTPUT) != 0;
        if (!anyFile && !(type & IMAGE_FRAMEBUFFER))
            return true;

        const bool needLinearEXR =
            anyFile && m_writeEXR && !m_writeEXRApplyImaging;

        // Tonemapped output with imaging applied.
        const bool needTonemapped =
            (type & IMAGE_FRAMEBUFFER) ||
            (anyFile && ((m_writeEXR && m_writeEXRApplyImaging) || m_writePNG));
        if (!needLinearEXR && !needTonemapped)
            return true;

        std::vector<float> bX, bY, bZ, bAlpha, bW;
        SnapshotAccum(bX, bY, bZ, bAlpha, bW);

        // Straight linear EXR.
        if (needLinearEXR)
        {
            std::vector<RGBColor> rgb;
            std::vector<float> alpha;
            BuildDisplayImage(rgb, alpha, false, bX, bY, bZ, bAlpha, bW);
            if (!m_premultiplyAlpha)
            {
                for (size_t i = 0; i < rgb.size(); ++i)
                    rgb[i] *= alpha[i];
            }
            WriteOpenEXRImage(3, m_writeEXRHalf, 1, m_filename + ".exr", rgb,
                              alpha, m_xCount, m_yCount, m_xres, m_yres,
                              m_xStart, m_yStart);
        }

        if (!needTonemapped)
            return true;

        std::vector<RGBColor> rgb;
        std::vector<float> alpha;
        BuildDisplayImage(rgb, alpha, true, bX, bY, bZ, bAlpha, bW);

        if (anyFile && m_writeEXR && m_writeEXRApplyImaging)
        {
            std::vector<RGBColor> exrRgb = rgb;
            if (!m_premultiplyAlpha)
            {
                for (size_t i = 0; i < exrRgb.size(); ++i)
                    exrRgb[i] *= alpha[i];
            }
            WriteOpenEXRImage(3, m_writeEXRHalf, 1, m_filename + ".exr", exrRgb,
                              alpha, m_xCount, m_yCount, m_xres, m_yres,
                              m_xStart, m_yStart);
        }

        const ColorSystem cs(m_csRed[0], m_csRed[1], m_csGreen[0], m_csGreen[1],
                             m_csBlue[0], m_csBlue[1], m_csWhite[0],
                             m_csWhite[1], 1.f);
        const float invGamma = 1.f / m_gamma;

        // LDR output and the display buffer.
        std::vector<RGBColor> displayRgb(rgb.size());
        for (size_t i = 0; i < rgb.size(); ++i)
            displayRgb[i] = cs.Limit(rgb[i], 0).Pow(invGamma);

        if (anyFile && m_writePNG)
        {
            WritePngImage(2, m_writePNG16, m_filename + ".png", displayRgb,
                          alpha, m_xCount, m_yCount, m_xres, m_yres,
                          m_xStart, m_yStart, cs, m_gamma);
        }

        if (type & IMAGE_FRAMEBUFFER)
        {
            CreateFrameBuffer();
            size_t i = 0;
            for (int y = m_yStart; y < m_yStart + m_yCount; ++y)
            {
                for (int x = m_xStart; x < m_xStart + m_xCount; ++x, ++i)
                {
                    const size_t off = size_t(y) * m_xres + size_t(x);
                    const RGBColor limited = cs.Limit(rgb[i], 0);
                    m_floatFrameBuffer[3 * off] = limited[0];
                    m_floatFrameBuffer[3 * off + 1] = limited[1];
                    m_floatFrameBuffer[3 * off + 2] = limited[2];

                    const RGBColor &g = displayRgb[i];
                    m_frameBuffer[3 * off] = static_cast<unsigned char>(
                        ClampF(255.f * g[0], 0.f, 255.f));
                    m_frameBuffer[3 * off + 1] = static_cast<unsigned char>(
                        ClampF(255.f * g[1], 0.f, 255.f));
                    m_frameBuffer[3 * off + 2] = static_cast<unsigned char>(
                        ClampF(255.f * g[2], 0.f, 255.f));

                    m_alphaBuffer[off] = alpha[i];
                }
            }
        }
        return true;
    }

    void FlexImageFilm::SetParameterValue(luxComponentParameters param,
                                          double value, unsigned int)
    {
        switch (param)
        {
        case LUX_FILM_TM_TONEMAPKERNEL:
            m_tonemapKernel = int(value);
            break;
        case LUX_FILM_TM_REINHARD_PRESCALE:
            m_reinhardPreScale = float(value);
            break;
        case LUX_FILM_TM_REINHARD_POSTSCALE:
            m_reinhardPostScale = float(value);
            break;
        case LUX_FILM_TM_REINHARD_BURN:
            m_reinhardBurn = float(value);
            break;
        case LUX_FILM_TM_LINEAR_SENSITIVITY:
            m_linearSensitivity = float(value);
            break;
        case LUX_FILM_TM_LINEAR_EXPOSURE:
            m_linearExposure = float(value);
            break;
        case LUX_FILM_TM_LINEAR_FSTOP:
            m_linearFStop = float(value);
            break;
        case LUX_FILM_TM_LINEAR_GAMMA:
            m_linearGamma = float(value);
            break;
        case LUX_FILM_TORGB_X_WHITE:
            m_csWhite[0] = float(value);
            break;
        case LUX_FILM_TORGB_Y_WHITE:
            m_csWhite[1] = float(value);
            break;
        case LUX_FILM_TORGB_X_RED:
            m_csRed[0] = float(value);
            break;
        case LUX_FILM_TORGB_Y_RED:
            m_csRed[1] = float(value);
            break;
        case LUX_FILM_TORGB_X_GREEN:
            m_csGreen[0] = float(value);
            break;
        case LUX_FILM_TORGB_Y_GREEN:
            m_csGreen[1] = float(value);
            break;
        case LUX_FILM_TORGB_X_BLUE:
            m_csBlue[0] = float(value);
            break;
        case LUX_FILM_TORGB_Y_BLUE:
            m_csBlue[1] = float(value);
            break;
        default:
            // TORGB_GAMMA and unimplemented ids are ignored.
            break;
        }
    }

    double FlexImageFilm::GetParameterValue(luxComponentParameters param,
                                            unsigned int) const
    {
        switch (param)
        {
        case LUX_FILM_TM_TONEMAPKERNEL:
            return m_tonemapKernel;
        case LUX_FILM_TM_REINHARD_PRESCALE:
            return m_reinhardPreScale;
        case LUX_FILM_TM_REINHARD_POSTSCALE:
            return m_reinhardPostScale;
        case LUX_FILM_TM_REINHARD_BURN:
            return m_reinhardBurn;
        case LUX_FILM_TM_LINEAR_SENSITIVITY:
            return m_linearSensitivity;
        case LUX_FILM_TM_LINEAR_EXPOSURE:
            return m_linearExposure;
        case LUX_FILM_TM_LINEAR_FSTOP:
            return m_linearFStop;
        case LUX_FILM_TM_LINEAR_GAMMA:
            return m_linearGamma;
        case LUX_FILM_TORGB_X_WHITE:
            return m_csWhite[0];
        case LUX_FILM_TORGB_Y_WHITE:
            return m_csWhite[1];
        case LUX_FILM_TORGB_X_RED:
            return m_csRed[0];
        case LUX_FILM_TORGB_Y_RED:
            return m_csRed[1];
        case LUX_FILM_TORGB_X_GREEN:
            return m_csGreen[0];
        case LUX_FILM_TORGB_Y_GREEN:
            return m_csGreen[1];
        case LUX_FILM_TORGB_X_BLUE:
            return m_csBlue[0];
        case LUX_FILM_TORGB_Y_BLUE:
            return m_csBlue[1];
        default:
            return 0.0;
        }
    }

    double FlexImageFilm::GetDefaultParameterValue(luxComponentParameters param,
                                                   unsigned int) const
    {
        switch (param)
        {
        case LUX_FILM_TM_TONEMAPKERNEL:
            return m_dTonemapKernel;
        case LUX_FILM_TM_REINHARD_PRESCALE:
            return m_dReinhardPreScale;
        case LUX_FILM_TM_REINHARD_POSTSCALE:
            return m_dReinhardPostScale;
        case LUX_FILM_TM_REINHARD_BURN:
            return m_dReinhardBurn;
        case LUX_FILM_TM_LINEAR_SENSITIVITY:
            return m_dLinearSensitivity;
        case LUX_FILM_TM_LINEAR_EXPOSURE:
            return m_dLinearExposure;
        case LUX_FILM_TM_LINEAR_FSTOP:
            return m_dLinearFStop;
        case LUX_FILM_TM_LINEAR_GAMMA:
            return m_dLinearGamma;
        case LUX_FILM_TORGB_X_WHITE:
            return m_dCsWhite[0];
        case LUX_FILM_TORGB_Y_WHITE:
            return m_dCsWhite[1];
        case LUX_FILM_TORGB_X_RED:
            return m_dCsRed[0];
        case LUX_FILM_TORGB_Y_RED:
            return m_dCsRed[1];
        case LUX_FILM_TORGB_X_GREEN:
            return m_dCsGreen[0];
        case LUX_FILM_TORGB_Y_GREEN:
            return m_dCsGreen[1];
        case LUX_FILM_TORGB_X_BLUE:
            return m_dCsBlue[0];
        case LUX_FILM_TORGB_Y_BLUE:
            return m_dCsBlue[1];
        default:
            return 0.0;
        }
    }

    // -----------------------------------------------------------------------
    // Plugin factory
    // -----------------------------------------------------------------------

    namespace
    {
        void GetColorspaceParam(const ParamSet &params, const std::string &name,
                                float values[2])
        {
            std::uint32_t nItems;
            const float *v = params.FindFloat(name, &nItems);
            if (v && nItems == 2)
            {
                values[0] = v[0];
                values[1] = v[1];
            }
        }
    } // namespace

    std::shared_ptr<Film> FlexImageFilm::CreateFilm(const PluginContext &ctx)
    {
        const ParamSet *p = ctx.params;
        ParamSet empty;
        const ParamSet &params = p ? *p : empty;

        const int xres = params.FindOneInt("xresolution", 800);
        const int yres = params.FindOneInt("yresolution", 600);

        float crop[4] = {0.f, 1.f, 0.f, 1.f};
        std::uint32_t cwi = 0;
        const float *cr = params.FindFloat("cropwindow", &cwi);
        if (cr && cwi == 4)
        {
            crop[0] = std::clamp(std::min(cr[0], cr[1]), 0.f, 1.f);
            crop[1] = std::clamp(std::max(cr[0], cr[1]), 0.f, 1.f);
            crop[2] = std::clamp(std::min(cr[2], cr[3]), 0.f, 1.f);
            crop[3] = std::clamp(std::max(cr[2], cr[3]), 0.f, 1.f);
        }

        const bool premult = params.FindOneBool("premultiplyalpha", false);
        const std::string filename = params.FindOneString("filename", "luxout");

        auto film = std::make_shared<FlexImageFilm>(
            xres, yres, ctx.filter, crop, filename, premult);

        // Output configuration.
        film->m_writeEXR = params.FindOneBool("write_exr", false);
        film->m_writeEXRHalf = params.FindOneBool("write_exr_halftype", true);
        film->m_writeEXRApplyImaging =
            params.FindOneBool("write_exr_applyimaging", true);
        film->m_writePNG = params.FindOneBool("write_png", true);
        film->m_writePNG16 = params.FindOneBool("write_png_16bit", false);
        film->m_writeInterval = params.FindOneInt("writeinterval", 60);
        film->m_flmWriteInterval =
            params.FindOneInt("flmwriteinterval", film->m_writeInterval);
        film->m_displayInterval = params.FindOneInt("displayinterval", 12);
        film->m_haltspp = params.FindOneInt("haltspp", -1);
        film->m_halttime = params.FindOneInt("halttime", -1);

        // Tonemap kernel.
        const std::string tmk = params.FindOneString("tonemapkernel", "autolinear");
        if (tmk == "linear")
            film->m_tonemapKernel = film->m_dTonemapKernel = TMK_LINEAR;
        else if (tmk == "autolinear")
            film->m_tonemapKernel = film->m_dTonemapKernel = TMK_AUTOLINEAR;
        else
        {
            LOG(LUX_WARNING, LUX_BADTOKEN)
                << "Tonemap kernel '" << tmk
                << "' not supported by lux2. Using \"autolinear\".";
            film->m_tonemapKernel = film->m_dTonemapKernel = TMK_AUTOLINEAR;
        }

        film->m_reinhardPreScale = film->m_dReinhardPreScale =
            params.FindOneFloat("reinhard_prescale", 1.f);
        film->m_reinhardPostScale = film->m_dReinhardPostScale =
            params.FindOneFloat("reinhard_postscale", 1.f);
        film->m_reinhardBurn = film->m_dReinhardBurn =
            params.FindOneFloat("reinhard_burn", 6.f);
        film->m_linearSensitivity = film->m_dLinearSensitivity =
            params.FindOneFloat("linear_sensitivity", 50.f);
        film->m_linearExposure = film->m_dLinearExposure =
            params.FindOneFloat("linear_exposure", 1.f);
        film->m_linearFStop = film->m_dLinearFStop =
            params.FindOneFloat("linear_fstop", 2.8f);
        film->m_linearGamma = film->m_dLinearGamma =
            params.FindOneFloat("linear_gamma", 1.f);
        film->m_gamma = film->m_dGamma = params.FindOneFloat("gamma", 2.2f);

        GetColorspaceParam(params, "colorspace_red", film->m_csRed);
        GetColorspaceParam(params, "colorspace_green", film->m_csGreen);
        GetColorspaceParam(params, "colorspace_blue", film->m_csBlue);
        GetColorspaceParam(params, "colorspace_white", film->m_csWhite);
        std::copy(std::begin(film->m_csRed), std::end(film->m_csRed),
                  std::begin(film->m_dCsRed));
        std::copy(std::begin(film->m_csGreen), std::end(film->m_csGreen),
                  std::begin(film->m_dCsGreen));
        std::copy(std::begin(film->m_csBlue), std::end(film->m_csBlue),
                  std::begin(film->m_dCsBlue));
        std::copy(std::begin(film->m_csWhite), std::end(film->m_csWhite),
                  std::begin(film->m_dCsWhite));

        return film;
    }

    LUX2_REGISTER_FILM(FlexImageFilm, "fleximage");
    LUX2_REGISTER_FILM(FlexImageFilm, "multiimage");

} // namespace lux2
