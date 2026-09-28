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
#include "core/dynload.h"
#include "core/paramset.h"
#include "core/register.h"

#include <algorithm>
#include <cmath>

namespace lux2
{

    namespace
    {
        // Crop window math.
        int CeilToInt(float v) { return int(std::ceil(v)); }
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

    std::unique_ptr<Film> FlexImageFilm::MakePrivateBlock(int x0, int y0,
                                                          int x1, int y1) const
    {
        // Clamp the requested rect to this film's crop window.
        const int cx0 = std::max(x0, m_xStart);
        const int cy0 = std::max(y0, m_yStart);
        const int cx1 = std::min(x1, m_xStart + m_xCount);
        const int cy1 = std::min(y1, m_yStart + m_yCount);
        if (cx1 <= cx0 || cy1 <= cy0)
            return nullptr;

        return std::unique_ptr<Film>(new FlexImageFilm(
            m_xres, m_yres, m_filter, cx0, cx1 - cx0, cy0, cy1 - cy0,
            m_premultiplyAlpha));
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

        // Reject non-finite or negative Y/alpha/weight.
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

    void FlexImageFilm::MergeRegion(Film *other, int x0, int y0, int x1, int y1)
    {
        FlexImageFilm *o = dynamic_cast<FlexImageFilm *>(other);
        if (!o || o == this)
            return;

        // Intersection of the requested rect with both crop windows.
        const int cx0 = std::max(x0, std::max(m_xStart, o->m_xStart));
        const int cy0 = std::max(y0, std::max(m_yStart, o->m_yStart));
        const int cx1 = std::min(x1, std::min(m_xStart + m_xCount,
                                              o->m_xStart + o->m_xCount));
        const int cy1 = std::min(y1, std::min(m_yStart + m_yCount,
                                              o->m_yStart + o->m_yCount));
        if (cx1 <= cx0 || cy1 <= cy0)
            return;

        // One lock per region merge.
        std::lock_guard<std::mutex> lock(m_mergeMutex);

        const int rowLen = cx1 - cx0;
        for (int y = cy0; y < cy1; ++y)
        {
            float *dstRow = m_bX.data() + size_t(y - m_yStart) * m_xCount +
                            (cx0 - m_xStart);
            float *srcRow = o->m_bX.data() + size_t(y - o->m_yStart) *
                                                 o->m_xCount +
                            (cx0 - o->m_xStart);
            float *dstRowY = m_bY.data() + size_t(y - m_yStart) * m_xCount +
                             (cx0 - m_xStart);
            float *srcRowY = o->m_bY.data() + size_t(y - o->m_yStart) *
                                                  o->m_xCount +
                             (cx0 - o->m_xStart);
            float *dstRowZ = m_bZ.data() + size_t(y - m_yStart) * m_xCount +
                             (cx0 - m_xStart);
            float *srcRowZ = o->m_bZ.data() + size_t(y - o->m_yStart) *
                                                  o->m_xCount +
                             (cx0 - o->m_xStart);
            float *dstRowA = m_bAlpha.data() +
                             size_t(y - m_yStart) * m_xCount +
                             (cx0 - m_xStart);
            float *srcRowA = o->m_bAlpha.data() +
                             size_t(y - o->m_yStart) * o->m_xCount +
                             (cx0 - o->m_xStart);
            float *dstRowW = m_bW.data() + size_t(y - m_yStart) * m_xCount +
                             (cx0 - m_xStart);
            float *srcRowW = o->m_bW.data() + size_t(y - o->m_yStart) *
                                                 o->m_xCount +
                             (cx0 - o->m_xStart);
            for (int i = 0; i < rowLen; ++i)
            {
                dstRow[i] += srcRow[i];
                dstRowY[i] += srcRowY[i];
                dstRowZ[i] += srcRowZ[i];
                dstRowA[i] += srcRowA[i];
                dstRowW[i] += srcRowW[i];
                srcRow[i] = 0.f;
                srcRowY[i] = 0.f;
                srcRowZ[i] = 0.f;
                srcRowA[i] = 0.f;
                srcRowW[i] = 0.f;
            }
        }
        m_sampleCount += o->m_sampleCount;
        o->m_sampleCount = 0.0;
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

    bool FlexImageFilm::WriteImage(ImageType type)
    {
        // File output.
        if (type & (IMAGE_FILEOUTPUT | IMAGE_FLMOUTPUT))
        {
            LOG(LUX_WARNING, LUX_UNIMPLEMENT)
                << LUX2_UNSUPPORTED_TAG << " FlexImageFilm image/FLM output";
            return false;
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
