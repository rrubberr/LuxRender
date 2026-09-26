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

    void FlexImageFilm::AddFiltered(float x, float y, const float xyz[3],
                                    float alpha, float weight)
    {
        // Filter footprint in frame coordinates.
        const float xWidth = m_filter ? m_filter->GetXWidth() : 0.5f;
        const float yWidth = m_filter ? m_filter->GetYWidth() : 0.5f;
        const float dx = x - 0.5f;
        const float dy = y - 0.5f;

        const int x0 = CeilToInt(dx - xWidth);
        const int x1 = int(std::floor(dx + xWidth));
        const int y0 = CeilToInt(dy - yWidth);
        const int y1 = int(std::floor(dy + yWidth));

        // Normalize each sample's filter footprint to unit total.
        float total = 0.f;
        if (m_filter)
        {
            for (int iy = y0; iy <= y1; ++iy)
                for (int ix = x0; ix <= x1; ++ix)
                    total += m_filter->Evaluate(std::fabs(float(ix) - dx),
                                                std::fabs(float(iy) - dy));
        }

        const int xEnd = std::min(x1, m_xStart + m_xCount - 1);
        const int yEnd = std::min(y1, m_yStart + m_yCount - 1);
        for (int iy = std::max(y0, m_yStart); iy <= yEnd; ++iy)
        {
            for (int ix = std::max(x0, m_xStart); ix <= xEnd; ++ix)
            {
                float w;
                if (m_filter && total > 0.f)
                    w = weight * m_filter->Evaluate(std::fabs(float(ix) - dx),
                                                    std::fabs(float(iy) - dy)) /
                        total;
                else
                    w = weight;

                const size_t idx = size_t(iy - m_yStart) * m_xCount +
                                   size_t(ix - m_xStart);
                m_bX[idx] += w * xyz[0];
                m_bY[idx] += w * xyz[1];
                m_bZ[idx] += w * xyz[2];
                m_bAlpha[idx] += alpha * w;
                m_bW[idx] += w;
            }
        }
    }

    void FlexImageFilm::Splat(const FloatP &x, const FloatP &y,
                              const SWCSpectrumP &L,
                              const SpectrumWavelengthsP &sw,
                              const FloatP &alpha, const FloatP &weight,
                              int)
    {
        const XYZColorP xyzP = SWCToXYZ(L, sw);

        float xA[PACKET_WIDTH], yA[PACKET_WIDTH];
        float xa[PACKET_WIDTH], ya[PACKET_WIDTH], za[PACKET_WIDTH];
        float alphaA[PACKET_WIDTH], weightA[PACKET_WIDTH];
        enoki::store_unaligned(xA, x);
        enoki::store_unaligned(yA, y);
        enoki::store_unaligned(xa, xyzP[0]);
        enoki::store_unaligned(ya, xyzP[1]);
        enoki::store_unaligned(za, xyzP[2]);
        enoki::store_unaligned(alphaA, alpha);
        enoki::store_unaligned(weightA, weight);

        for (size_t i = 0; i < PACKET_WIDTH; ++i)
        {
            // Legacy validity: reject non-finite or negative Y/alpha/weight.
            if (!(ya[i] >= 0.f) || !std::isfinite(ya[i]) ||
                !(alphaA[i] >= 0.f) || !std::isfinite(alphaA[i]) ||
                !(weightA[i] >= 0.f) || !std::isfinite(weightA[i]))
                continue;

            float xyz[3] = {xa[i], ya[i], za[i]};
            if (m_premultiplyAlpha)
            {
                xyz[0] *= alphaA[i];
                xyz[1] *= alphaA[i];
                xyz[2] *= alphaA[i];
            }

            AddFiltered(xA[i], yA[i], xyz, alphaA[i], weightA[i]);
        }
    }

    void FlexImageFilm::Merge(Film *other)
    {
        FlexImageFilm *o = dynamic_cast<FlexImageFilm *>(other);
        if (!o || o == this)
            return;
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
        case LUX_FILM_TM_TONEMAPKERNEL: m_tonemapKernel = int(value); break;
        case LUX_FILM_TM_REINHARD_PRESCALE: m_reinhardPreScale = float(value); break;
        case LUX_FILM_TM_REINHARD_POSTSCALE: m_reinhardPostScale = float(value); break;
        case LUX_FILM_TM_REINHARD_BURN: m_reinhardBurn = float(value); break;
        case LUX_FILM_TM_LINEAR_SENSITIVITY: m_linearSensitivity = float(value); break;
        case LUX_FILM_TM_LINEAR_EXPOSURE: m_linearExposure = float(value); break;
        case LUX_FILM_TM_LINEAR_FSTOP: m_linearFStop = float(value); break;
        case LUX_FILM_TM_LINEAR_GAMMA: m_linearGamma = float(value); break;
        case LUX_FILM_TORGB_X_WHITE: m_csWhite[0] = float(value); break;
        case LUX_FILM_TORGB_Y_WHITE: m_csWhite[1] = float(value); break;
        case LUX_FILM_TORGB_X_RED: m_csRed[0] = float(value); break;
        case LUX_FILM_TORGB_Y_RED: m_csRed[1] = float(value); break;
        case LUX_FILM_TORGB_X_GREEN: m_csGreen[0] = float(value); break;
        case LUX_FILM_TORGB_Y_GREEN: m_csGreen[1] = float(value); break;
        case LUX_FILM_TORGB_X_BLUE: m_csBlue[0] = float(value); break;
        case LUX_FILM_TORGB_Y_BLUE: m_csBlue[1] = float(value); break;
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
        case LUX_FILM_TM_TONEMAPKERNEL: return m_tonemapKernel;
        case LUX_FILM_TM_REINHARD_PRESCALE: return m_reinhardPreScale;
        case LUX_FILM_TM_REINHARD_POSTSCALE: return m_reinhardPostScale;
        case LUX_FILM_TM_REINHARD_BURN: return m_reinhardBurn;
        case LUX_FILM_TM_LINEAR_SENSITIVITY: return m_linearSensitivity;
        case LUX_FILM_TM_LINEAR_EXPOSURE: return m_linearExposure;
        case LUX_FILM_TM_LINEAR_FSTOP: return m_linearFStop;
        case LUX_FILM_TM_LINEAR_GAMMA: return m_linearGamma;
        case LUX_FILM_TORGB_X_WHITE: return m_csWhite[0];
        case LUX_FILM_TORGB_Y_WHITE: return m_csWhite[1];
        case LUX_FILM_TORGB_X_RED: return m_csRed[0];
        case LUX_FILM_TORGB_Y_RED: return m_csRed[1];
        case LUX_FILM_TORGB_X_GREEN: return m_csGreen[0];
        case LUX_FILM_TORGB_Y_GREEN: return m_csGreen[1];
        case LUX_FILM_TORGB_X_BLUE: return m_csBlue[0];
        case LUX_FILM_TORGB_Y_BLUE: return m_csBlue[1];
        default: return 0.0;
        }
    }

    double FlexImageFilm::GetDefaultParameterValue(luxComponentParameters param,
                                                   unsigned int) const
    {
        switch (param)
        {
        case LUX_FILM_TM_TONEMAPKERNEL: return m_dTonemapKernel;
        case LUX_FILM_TM_REINHARD_PRESCALE: return m_dReinhardPreScale;
        case LUX_FILM_TM_REINHARD_POSTSCALE: return m_dReinhardPostScale;
        case LUX_FILM_TM_REINHARD_BURN: return m_dReinhardBurn;
        case LUX_FILM_TM_LINEAR_SENSITIVITY: return m_dLinearSensitivity;
        case LUX_FILM_TM_LINEAR_EXPOSURE: return m_dLinearExposure;
        case LUX_FILM_TM_LINEAR_FSTOP: return m_dLinearFStop;
        case LUX_FILM_TM_LINEAR_GAMMA: return m_dLinearGamma;
        case LUX_FILM_TORGB_X_WHITE: return m_dCsWhite[0];
        case LUX_FILM_TORGB_Y_WHITE: return m_dCsWhite[1];
        case LUX_FILM_TORGB_X_RED: return m_dCsRed[0];
        case LUX_FILM_TORGB_Y_RED: return m_dCsRed[1];
        case LUX_FILM_TORGB_X_GREEN: return m_dCsGreen[0];
        case LUX_FILM_TORGB_Y_GREEN: return m_dCsGreen[1];
        case LUX_FILM_TORGB_X_BLUE: return m_dCsBlue[0];
        case LUX_FILM_TORGB_Y_BLUE: return m_dCsBlue[1];
        default: return 0.0;
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
