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

#include "textures/imagemap.h"

#include "core/dynload.h"
#include "core/paramset.h"
#include "core/register.h"
#include "core/color/color.h"
#include "core/imageio/readimage.h"

#include <cmath>

namespace lux2
{

    // Parse common imagemap parameters.
    namespace
    {
        struct ImageParams
        {
            ImageFilterType filterType;
            ImageWrap wrapMode;
            float maxAniso;
            float gain;
            float gamma;
            std::string filename;
            int discardmm;
        };

        ImageParams ParseImageParams(const PluginContext &ctx)
        {
            ImageParams p;

            const std::string sFilter =
                ctx.params->FindOneString("filtertype", "bilinear");
            if (sFilter == "nearest")
                p.filterType = ImageFilterType::Nearest;
            else if (sFilter == "mipmap_trilinear")
                p.filterType = ImageFilterType::MipmapTrilinear;
            else if (sFilter == "mipmap_ewa")
                p.filterType = ImageFilterType::MipmapEWA;
            else
                p.filterType = ImageFilterType::Bilinear;

            const std::string sWrap = ctx.params->FindOneString("wrap", "repeat");
            if (sWrap == "black")
                p.wrapMode = ImageWrap::Black;
            else if (sWrap == "white")
                p.wrapMode = ImageWrap::White;
            else if (sWrap == "clamp")
                p.wrapMode = ImageWrap::Clamp;
            else
                p.wrapMode = ImageWrap::Repeat;

            p.maxAniso = ctx.params->FindOneFloat("maxanisotropy", 8.f);
            p.gain = ctx.params->FindOneFloat("gain", 1.f);
            p.gamma = ctx.params->FindOneFloat("gamma", 1.f);
            p.filename = ctx.params->FindOneString("filename", "");
            p.discardmm = ctx.params->FindOneInt("discardmipmaps", 0);

            return p;
        }
    } // namespace

    // =========================================================================
    // ImageColorTexture
    // =========================================================================

    SWCSpectrumP ImageColorTexture::Evaluate(const DifferentialGeometryP &dg,
                                             const SpectrumWavelengthsP &sw,
                                             MaskP active) const
    {
        // Extract per-lane UV and footprint.
        alignas(64) float uArr[PACKET_WIDTH], vArr[PACKET_WIDTH];
        enoki::store_unaligned(uArr, dg.uv_u);
        enoki::store_unaligned(vArr, dg.uv_v);

        // Width=0 selects finest level.
        const float width = 0.f;

        alignas(64) float rArr[PACKET_WIDTH], gArr[PACKET_WIDTH], bArr[PACKET_WIDTH];
        for (size_t i = 0; i < PACKET_WIDTH; ++i)
        {
            float rgb[3];
            m_mipmap->LookupRGB(uArr[i], vArr[i], width, rgb);
            // MIPMapGammaImpl: pow(gain * raw, gamma) per component.
            rArr[i] = std::pow(m_gain * rgb[0], m_gamma);
            gArr[i] = std::pow(m_gain * rgb[1], m_gamma);
            bArr[i] = std::pow(m_gain * rgb[2], m_gamma);
        }

        const RGBColorP rgb(enoki::load_unaligned<FloatP>(rArr),
                            enoki::load_unaligned<FloatP>(gArr),
                            enoki::load_unaligned<FloatP>(bArr));
        return RGBToSmitsSPD(rgb, sw, m_isIlluminant);
    }

    std::shared_ptr<ColorTexture> ImageColorTexture::CreateColorTexture(
        const PluginContext &ctx)
    {
        ImageParams p = ParseImageParams(ctx);
        auto mipmap = GetCachedMIPMap(
            TexInfo{p.filterType, p.filename, p.discardmm, p.maxAniso, p.wrapMode});
        return std::make_shared<ImageColorTexture>(mipmap, p.gain, p.gamma, false);
    }

    // =========================================================================
    // ImageFloatTexture
    // =========================================================================

    FloatP ImageFloatTexture::Evaluate(const DifferentialGeometryP &dg,
                                       const SpectrumWavelengthsP &,
                                       MaskP) const
    {
        alignas(64) float uArr[PACKET_WIDTH], vArr[PACKET_WIDTH];
        enoki::store_unaligned(uArr, dg.uv_u);
        enoki::store_unaligned(vArr, dg.uv_v);

        const float width = 0.f;

        alignas(64) float out[PACKET_WIDTH];
        for (size_t i = 0; i < PACKET_WIDTH; ++i)
        {
            const float raw = m_mipmap->LookupFloat(m_channel, uArr[i], vArr[i], width);
            out[i] = std::pow(m_gain * raw, m_gamma);
        }
        return enoki::load_unaligned<FloatP>(out);
    }

    std::shared_ptr<FloatTexture> ImageFloatTexture::CreateFloatTexture(
        const PluginContext &ctx)
    {
        ImageParams p = ParseImageParams(ctx);

        const std::string channel = ctx.params->FindOneString("channel", "mean");
        TexChannel ch;
        if (channel == "red")
            ch = TexChannel::Red;
        else if (channel == "green")
            ch = TexChannel::Green;
        else if (channel == "blue")
            ch = TexChannel::Blue;
        else if (channel == "alpha")
            ch = TexChannel::Alpha;
        else if (channel == "colored_mean")
            ch = TexChannel::WMean;
        else
            ch = TexChannel::Mean;

        auto mipmap = GetCachedMIPMap(
            TexInfo{p.filterType, p.filename, p.discardmm, p.maxAniso, p.wrapMode});
        return std::make_shared<ImageFloatTexture>(mipmap, p.gain, p.gamma, ch);
    }

    // =========================================================================
    // NormalMapTexture
    // =========================================================================

    void NormalMapTexture::GetDuv(const DifferentialGeometryP &dg,
                                  FloatP *du, FloatP *dv) const
    {
        alignas(64) float uArr[PACKET_WIDTH], vArr[PACKET_WIDTH];
        enoki::store_unaligned(uArr, dg.uv_u);
        enoki::store_unaligned(vArr, dg.uv_v);

        const float width = 0.f;

        // Extract shading frame components per lane.
        alignas(64) float dsx[PACKET_WIDTH], dsy[PACKET_WIDTH], dsz[PACKET_WIDTH];
        alignas(64) float dtx[PACKET_WIDTH], dty[PACKET_WIDTH], dtz[PACKET_WIDTH];
        alignas(64) float nx[PACKET_WIDTH], ny[PACKET_WIDTH], nz[PACKET_WIDTH];
        alignas(64) float dpdux[PACKET_WIDTH], dpduy[PACKET_WIDTH], dpduz[PACKET_WIDTH];
        alignas(64) float dpdvx[PACKET_WIDTH], dpdvy[PACKET_WIDTH], dpdvz[PACKET_WIDTH];

        enoki::store_unaligned(dsx, dg.dp_ds.x());
        enoki::store_unaligned(dsy, dg.dp_ds.y());
        enoki::store_unaligned(dsz, dg.dp_ds.z());
        enoki::store_unaligned(dtx, dg.dp_dt.x());
        enoki::store_unaligned(dty, dg.dp_dt.y());
        enoki::store_unaligned(dtz, dg.dp_dt.z());
        enoki::store_unaligned(nx, dg.n.x());
        enoki::store_unaligned(ny, dg.n.y());
        enoki::store_unaligned(nz, dg.n.z());
        enoki::store_unaligned(dpdux, dg.dp_du.x());
        enoki::store_unaligned(dpduy, dg.dp_du.y());
        enoki::store_unaligned(dpduz, dg.dp_du.z());
        enoki::store_unaligned(dpdvx, dg.dp_dv.x());
        enoki::store_unaligned(dpdvy, dg.dp_dv.y());
        enoki::store_unaligned(dpdvz, dg.dp_dv.z());

        alignas(64) float duOut[PACKET_WIDTH], dvOut[PACKET_WIDTH];

        for (size_t i = 0; i < PACKET_WIDTH; ++i)
        {
            // Decode tangent space normal from RGBA texel.
            float rgba[4];
            m_mipmap->LookupRGBA(uArr[i], vArr[i], width, rgba);
            float tn[3] = {2.f * rgba[0] - 1.f,
                           2.f * rgba[1] - 1.f,
                           2.f * rgba[2] - 1.f};

            // Transform tangent -> object: n_obj = tn.x*T + tn.y*B + tn.z*K
            float nox = tn[0] * dsx[i] + tn[1] * dtx[i] + tn[2] * nx[i];
            float noy = tn[0] * dsy[i] + tn[1] * dty[i] + tn[2] * ny[i];
            float noz = tn[0] * dsz[i] + tn[1] * dtz[i] + tn[2] * nz[i];

            // Normalize.
            const float len = std::sqrt(nox * nox + noy * noy + noz * noz);
            if (len > 1e-10f)
            {
                const float inv = 1.f / len;
                nox *= inv;
                noy *= inv;
                noz *= inv;
            }

            // nn = (-1 / Dot(k, n_obj)) * n_obj
            const float kdotn = nx[i] * nox + ny[i] * noy + nz[i] * noz;
            const float scale = (std::fabs(kdotn) > 1e-10f) ? (-1.f / kdotn) : 0.f;
            const float nnx = scale * nox;
            const float nny = scale * noy;
            const float nnz = scale * noz;

            duOut[i] = dpdux[i] * nnx + dpduy[i] * nny + dpduz[i] * nnz;
            dvOut[i] = dpdvx[i] * nnx + dpdvy[i] * nny + dpdvz[i] * nnz;
        }

        *du = enoki::load_unaligned<FloatP>(duOut);
        *dv = enoki::load_unaligned<FloatP>(dvOut);
    }

    std::shared_ptr<FloatTexture> NormalMapTexture::CreateFloatTexture(
        const PluginContext &ctx)
    {
        ImageParams p = ParseImageParams(ctx);
        auto mipmap = GetCachedMIPMap(
            TexInfo{p.filterType, p.filename, p.discardmm, p.maxAniso, p.wrapMode});
        return std::make_shared<NormalMapTexture>(mipmap, p.gain, p.gamma);
    }

    // =========================================================================
    // Registration
    // =========================================================================

    LUX2_REGISTER_COLOR_TEXTURE(ImageColorTexture, "imagemap");
    LUX2_REGISTER_FLOAT_TEXTURE(ImageFloatTexture, "imagemap");
    LUX2_REGISTER_FLOAT_TEXTURE(NormalMapTexture, "normalmap");

} // namespace lux2
