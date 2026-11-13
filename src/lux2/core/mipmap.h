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

#ifndef LUX2_MIPMAP_H
#define LUX2_MIPMAP_H

#include "core/math/math.h"

#include <vector>

namespace lux2
{

    // MipmapEWA is accepted for compatibility but falls back to trilinear.
    enum class ImageFilterType
    {
        Nearest,
        Bilinear,
        MipmapTrilinear,
        MipmapEWA
    };

    // OOB texture coordinate handling.
    enum class ImageWrap
    {
        Repeat,
        Black,
        White,
        Clamp
    };

    // Scalar channel selector for float textures.
    enum class TexChannel
    {
        Red,
        Green,
        Blue,
        Alpha,
        Mean,
        WMean
    };

    // A mipmapped image
    class MIPMap
    {
    public:
        // data: xres*yres*channels interleaved floats, normalized to
        // the [0,1] (or HDR) range. channels is 1, 3, or 4.
        MIPMap(ImageFilterType type, int xres, int yres, int channels,
               const float *data, float maxAniso = 8.f,
               ImageWrap wrapMode = ImageWrap::Repeat);

        // Filtered lookups at (s,t) with footprint width. Output raw
        // (no gain/gamma).
        void LookupRGB(float s, float t, float width, float out[3]) const;
        void LookupRGBA(float s, float t, float width, float out[4]) const;
        float LookupFloat(TexChannel ch, float s, float t, float width) const;

        // Min/max of a channel over the finest level.
        void GetMinMax(TexChannel ch, float *minValue, float *maxValue) const;

        // Drop n finest pyramid levels.
        void DiscardMipmaps(int n);

        int USize() const { return m_levels.empty() ? m_single.us : m_levels[0].us; }
        int VSize() const { return m_levels.empty() ? m_single.vs : m_levels[0].vs; }
        int Channels() const { return m_channels; }
        ImageFilterType FilterType() const { return m_filterType; }

    private:
        struct Level
        {
            int us = 0, vs = 0;
            std::vector<float> data; // interleaved channels
        };

        // Raw texel with wrap applied.
        void Texel(int level, int s, int t, float out[4]) const;
        // Single map texel (nearest/bilinear).
        void TexelSingle(int s, int t, float out[4]) const;

        // Bilinear sample at a pyramid level / the single map.
        void Triangle(int level, float s, float t, float out[4]) const;
        void TriangleSingle(float s, float t, float out[4]) const;
        // Nearest sample from the single map.
        void Nearest(float s, float t, float out[4]) const;

        // Resolve a channel value from an RGBA texel given the stored count.
        static float ChannelValue(const float px[4], int channels, TexChannel ch);

        void ApplyWrap(int level, int &s, int &t, bool &inside) const;

        ImageFilterType m_filterType;
        ImageWrap m_wrapMode;
        float m_maxAniso;
        int m_channels;

        // Pyramid for trilinear/EWA; empty for nearest/bilinear.
        std::vector<Level> m_levels;
        // Single map for nearest/bilinear.
        Level m_single;
        int m_singleUs = 0, m_singleVs = 0;
    };

} // namespace lux2

#endif // LUX2_MIPMAP_H
