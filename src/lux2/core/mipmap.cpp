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

#include "core/mipmap.h"

#include "core/error.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lux2
{
    namespace
    {
        // Lanczos window, tau = 2.
        float Lanczos(float x, float tau = 2.f)
        {
            x = std::fabs(x);
            if (x < 1e-5f)
                return 1.f;
            if (x > 1.f)
                return 0.f;
            x *= PI;
            const float s = std::sin(x * tau) / (x * tau);
            const float lanczos = std::sin(x) / x;
            return s * lanczos;
        }

        inline int Floor2Int(float x) { return static_cast<int>(std::floor(x)); }
        inline int Mod(int a, int b)
        {
            int r = a % b;
            return (r < 0) ? r + b : r;
        }
        inline bool IsPowerOf2(int x) { return x && ((x & (x - 1)) == 0); }
        inline int RoundUpPow2(int x)
        {
            int r = 1;
            while (r < x)
                r <<= 1;
            return r;
        }
        inline int Log2Int(float x)
        {
            return static_cast<int>(std::log2(x));
        }

        struct ResampleWeight
        {
            int firstTexel;
            float weight[4];
        };

        // Lux MIPMapFastImpl::ResampleWeights.
        std::vector<ResampleWeight> ResampleWeights(int oldres, int newres)
        {
            std::vector<ResampleWeight> wt(newres);
            const float filterwidth = 2.f;
            for (int i = 0; i < newres; ++i)
            {
                const float center = (i + .5f) * oldres / newres;
                wt[i].firstTexel = Floor2Int(center - filterwidth + 0.5f);
                for (int j = 0; j < 4; ++j)
                {
                    const float pos = wt[i].firstTexel + j + .5f;
                    wt[i].weight[j] = Lanczos((pos - center) / filterwidth);
                }
                const float invSumWts =
                    1.f / (wt[i].weight[0] + wt[i].weight[1] +
                           wt[i].weight[2] + wt[i].weight[3]);
                for (int j = 0; j < 4; ++j)
                    wt[i].weight[j] *= invSumWts;
            }
            return wt;
        }
    } // namespace

    MIPMap::MIPMap(ImageFilterType type, int xres, int yres, int channels,
                   const float *data, float maxAniso, ImageWrap wrapMode)
        : m_filterType(type), m_wrapMode(wrapMode), m_maxAniso(maxAniso),
          m_channels(channels)
    {
        if (xres <= 0 || yres <= 0 || channels < 1 || channels > 4 || !data)
        {
            LOG(LUX_ERROR, LUX_BADFILE) << "MIPMap: invalid image dimensions";
            // Degenerate 1x1 white map so lookups stay safe.
            m_single.us = m_single.vs = 1;
            m_single.data.assign(channels, 1.f);
            m_singleUs = m_singleVs = 1;
            return;
        }

        const bool pyramid = (type == ImageFilterType::MipmapTrilinear ||
                              type == ImageFilterType::MipmapEWA);
        if (!pyramid)
        {
            // NEAREST / BILINEAR single map, no resample.
            m_single.us = xres;
            m_single.vs = yres;
            m_single.data.assign(data, data + size_t(xres) * yres * channels);
            m_singleUs = xres;
            m_singleVs = yres;
            return;
        }

        // Trilinear / EWA resample to power-of-two, then build the pyramid.
        const float *img = data;
        std::vector<float> resampled;
        if (!IsPowerOf2(xres) || !IsPowerOf2(yres))
        {
            const int sPow2 = RoundUpPow2(xres), tPow2 = RoundUpPow2(yres);
            LOG(LUX_INFO, LUX_NOERROR)
                << "Resampling image from " << xres << "x" << yres << " to "
                << sPow2 << "x" << tPow2;

            resampled.assign(size_t(sPow2) * tPow2 * channels, 0.f);

            // Zoom in s.
            std::vector<ResampleWeight> sWeights = ResampleWeights(xres, sPow2);
            for (int t = 0; t < yres; ++t)
            {
                for (int s = 0; s < sPow2; ++s)
                {
                    for (int c = 0; c < channels; ++c)
                    {
                        float acc = 0.f;
                        for (int j = 0; j < 4; ++j)
                        {
                            int origS = sWeights[s].firstTexel + j;
                            switch (wrapMode)
                            {
                            case ImageWrap::Repeat:
                                origS = Mod(origS, xres);
                                break;
                            case ImageWrap::Clamp:
                                origS = ClampF(origS, 0, xres - 1);
                                break;
                            default:
                                break;
                            }
                            if (origS >= 0 && origS < xres)
                                acc += sWeights[s].weight[j] *
                                       data[(size_t(t) * xres + origS) * channels + c];
                        }
                        resampled[(size_t(t) * sPow2 + s) * channels + c] = acc;
                    }
                }
            }

            // Zoom in t.
            std::vector<ResampleWeight> tWeights = ResampleWeights(yres, tPow2);
            std::vector<float> workData(tPow2);
            for (int s = 0; s < sPow2; ++s)
            {
                for (int t = 0; t < tPow2; ++t)
                {
                    for (int c = 0; c < channels; ++c)
                    {
                        float acc = 0.f;
                        for (int j = 0; j < 4; ++j)
                        {
                            int origT = tWeights[t].firstTexel + j;
                            switch (wrapMode)
                            {
                            case ImageWrap::Repeat:
                                origT = Mod(origT, yres);
                                break;
                            case ImageWrap::Clamp:
                                origT = ClampF(origT, 0, yres - 1);
                                break;
                            default:
                                break;
                            }
                            if (origT >= 0 && origT < yres)
                                acc += tWeights[t].weight[j] *
                                       resampled[(size_t(origT) * sPow2 + s) * channels + c];
                        }
                        workData[size_t(t) * channels + c] =
                            std::max(0.f, acc);
                    }
                }
                for (int t = 0; t < tPow2; ++t)
                    for (int c = 0; c < channels; ++c)
                        resampled[(size_t(t) * sPow2 + s) * channels + c] =
                            workData[size_t(t) * channels + c];
            }
            img = resampled.data();
            xres = sPow2;
            yres = tPow2;
        }

        const int nLevels =
            1 + std::max(0, Log2Int(float(std::max(xres, yres))));
        m_levels.resize(nLevels);
        m_levels[0].us = xres;
        m_levels[0].vs = yres;
        m_levels[0].data.assign(img, img + size_t(xres) * yres * channels);

        for (int i = 1; i < nLevels; ++i)
        {
            const int sRes = std::max(1, m_levels[i - 1].us / 2);
            const int tRes = std::max(1, m_levels[i - 1].vs / 2);
            m_levels[i].us = sRes;
            m_levels[i].vs = tRes;
            m_levels[i].data.resize(size_t(sRes) * tRes * channels);
            const Level &prev = m_levels[i - 1];
            for (int t = 0; t < tRes; ++t)
            {
                for (int s = 0; s < sRes; ++s)
                {
                    for (int c = 0; c < channels; ++c)
                    {
                        const float v =
                            0.25f * prev.data[(size_t(2 * t) * prev.us + 2 * s) * channels + c] +
                            0.25f * prev.data[(size_t(2 * t) * prev.us + (2 * s + 1)) * channels + c] +
                            0.25f * prev.data[(size_t((2 * t + 1)) * prev.us + 2 * s) * channels + c] +
                            0.25f * prev.data[(size_t((2 * t + 1)) * prev.us + (2 * s + 1)) * channels + c];
                        m_levels[i].data[(size_t(t) * sRes + s) * channels + c] = v;
                    }
                }
            }
        }
    }

    void MIPMap::ApplyWrap(int level, int &s, int &t, bool &inside) const
    {
        const Level &l = m_levels.empty() ? m_single : m_levels[level];
        inside = true;
        switch (m_wrapMode)
        {
        case ImageWrap::Repeat:
            s = Mod(s, l.us);
            t = Mod(t, l.vs);
            break;
        case ImageWrap::Clamp:
            s = ClampF(s, 0, l.us - 1);
            t = ClampF(t, 0, l.vs - 1);
            break;
        case ImageWrap::Black:
        case ImageWrap::White:
            if (s < 0 || s >= l.us || t < 0 || t >= l.vs)
                inside = false;
            break;
        }
    }

    void MIPMap::Texel(int level, int s, int t, float out[4]) const
    {
        bool inside = true;
        ApplyWrap(level, s, t, inside);
        const Level &l = m_levels[level];
        if (!inside)
        {
            const float fill = (m_wrapMode == ImageWrap::White) ? 1.f : 0.f;
            for (int c = 0; c < 4; ++c)
                out[c] = (c < m_channels) ? fill : 1.f;
            return;
        }
        const float *px = &l.data[(size_t(t) * l.us + s) * m_channels];
        for (int c = 0; c < 4; ++c)
            out[c] = (c < m_channels) ? px[c] : 1.f;
    }

    void MIPMap::TexelSingle(int s, int t, float out[4]) const
    {
        bool inside = true;
        ApplyWrap(0, s, t, inside);
        if (!inside)
        {
            const float fill = (m_wrapMode == ImageWrap::White) ? 1.f : 0.f;
            for (int c = 0; c < 4; ++c)
                out[c] = (c < m_channels) ? fill : 1.f;
            return;
        }
        const float *px = &m_single.data[(size_t(t) * m_single.us + s) * m_channels];
        for (int c = 0; c < 4; ++c)
            out[c] = (c < m_channels) ? px[c] : 1.f;
    }

    void MIPMap::Triangle(int level, float s, float t, float out[4]) const
    {
        level = ClampF(level, 0, int(m_levels.size()) - 1);
        const Level &l = m_levels[level];
        s *= l.us;
        t *= l.vs;
        s -= .5f;
        t -= .5f;
        const int s0 = Floor2Int(s), t0 = Floor2Int(t);
        const float ds = s - s0, dt = t - t0;

        float a[4], b[4], c[4], d[4];
        Texel(level, s0, t0, a);
        Texel(level, s0, t0 + 1, b);
        Texel(level, s0 + 1, t0, c);
        Texel(level, s0 + 1, t0 + 1, d);
        for (int ch = 0; ch < 4; ++ch)
        {
            const float top = a[ch] + dt * (b[ch] - a[ch]);
            const float bot = c[ch] + dt * (d[ch] - c[ch]);
            out[ch] = top + ds * (bot - top);
        }
    }

    void MIPMap::TriangleSingle(float s, float t, float out[4]) const
    {
        s *= m_single.us;
        t *= m_single.vs;
        s -= .5f;
        t -= .5f;
        const int s0 = Floor2Int(s), t0 = Floor2Int(t);
        const float ds = s - s0, dt = t - t0;

        float a[4], b[4], c[4], d[4];
        TexelSingle(s0, t0, a);
        TexelSingle(s0, t0 + 1, b);
        TexelSingle(s0 + 1, t0, c);
        TexelSingle(s0 + 1, t0 + 1, d);
        for (int ch = 0; ch < 4; ++ch)
        {
            const float top = a[ch] + dt * (b[ch] - a[ch]);
            const float bot = c[ch] + dt * (d[ch] - c[ch]);
            out[ch] = top + ds * (bot - top);
        }
    }

    void MIPMap::Nearest(float s, float t, float out[4]) const
    {
        s *= m_single.us;
        t *= m_single.vs;
        s -= .5f;
        t -= .5f;
        TexelSingle(Floor2Int(s), Floor2Int(t), out);
    }

    void MIPMap::LookupRGB(float s, float t, float width, float out[3]) const
    {
        float px[4];
        if (m_levels.empty())
        {
            if (m_filterType == ImageFilterType::Nearest)
                Nearest(s, t, px);
            else
                TriangleSingle(s, t, px);
        }
        else
        {
            const int nLevels = int(m_levels.size());
            const float level = nLevels - 1 + std::log2(std::max(width, 1e-8f));
            if (level < 0.f)
                Triangle(0, s, t, px);
            else if (level >= nLevels - 1)
                Texel(nLevels - 1, Floor2Int(s * m_levels[nLevels - 1].us),
                      Floor2Int(t * m_levels[nLevels - 1].vs), px);
            else
            {
                const int iLevel = Floor2Int(level);
                const float delta = level - iLevel;
                float lo[4], hi[4];
                Triangle(iLevel, s, t, lo);
                Triangle(iLevel + 1, s, t, hi);
                for (int c = 0; c < 4; ++c)
                    px[c] = lo[c] + delta * (hi[c] - lo[c]);
            }
        }
        out[0] = px[0];
        out[1] = m_channels >= 3 ? px[1] : px[0];
        out[2] = m_channels >= 3 ? px[2] : px[0];
    }

    void MIPMap::LookupRGBA(float s, float t, float width, float out[4]) const
    {
        if (m_levels.empty())
        {
            if (m_filterType == ImageFilterType::Nearest)
                Nearest(s, t, out);
            else
                TriangleSingle(s, t, out);
            return;
        }
        const int nLevels = int(m_levels.size());
        const float level = nLevels - 1 + std::log2(std::max(width, 1e-8f));
        if (level < 0.f)
            Triangle(0, s, t, out);
        else if (level >= nLevels - 1)
            Texel(nLevels - 1, Floor2Int(s * m_levels[nLevels - 1].us),
                  Floor2Int(t * m_levels[nLevels - 1].vs), out);
        else
        {
            const int iLevel = Floor2Int(level);
            const float delta = level - iLevel;
            float lo[4], hi[4];
            Triangle(iLevel, s, t, lo);
            Triangle(iLevel + 1, s, t, hi);
            for (int c = 0; c < 4; ++c)
                out[c] = lo[c] + delta * (hi[c] - lo[c]);
        }
    }

    float MIPMap::ChannelValue(const float px[4], int channels, TexChannel ch)
    {
        switch (ch)
        {
        case TexChannel::Red:
            return px[0];
        case TexChannel::Green:
            return channels >= 3 ? px[1] : px[0];
        case TexChannel::Blue:
            return channels >= 3 ? px[2] : px[0];
        case TexChannel::Alpha:
            return channels >= 4 ? px[3] : 1.f;
        case TexChannel::Mean:
            return channels >= 3 ? (px[0] + px[1] + px[2]) * (1.f / 3.f) : px[0];
        case TexChannel::WMean:
            return channels >= 3
                       ? 0.212671f * px[0] + 0.715160f * px[1] +
                             0.072169f * px[2]
                       : px[0];
        }
        return px[0];
    }

    float MIPMap::LookupFloat(TexChannel ch, float s, float t, float width) const
    {
        float px[4];
        LookupRGBA(s, t, width, px);
        return ChannelValue(px, m_channels, ch);
    }

    void MIPMap::DiscardMipmaps(int n)
    {
        for (int i = 0; i < n; ++i)
        {
            if (m_levels.size() <= 1)
                return;
            m_levels.erase(m_levels.begin());
        }
    }

    void MIPMap::GetMinMax(TexChannel ch, float *minValue, float *maxValue) const
    {
        const Level &map = m_levels.empty() ? m_single : m_levels[0];
        float minv = INF_F, maxv = -INF_F;
        for (int t = 0; t < map.vs; ++t)
        {
            for (int s = 0; s < map.us; ++s)
            {
                const float *px = &map.data[(size_t(t) * map.us + s) * m_channels];
                float tmp[4] = {px[0],
                                m_channels >= 2 ? px[1] : px[0],
                                m_channels >= 3 ? px[2] : px[0],
                                m_channels >= 4 ? px[3] : 1.f};
                const float v = ChannelValue(tmp, m_channels, ch);
                minv = std::min(minv, v);
                maxv = std::max(maxv, v);
            }
        }
        *minValue = minv;
        *maxValue = maxv;
    }

} // namespace lux2
