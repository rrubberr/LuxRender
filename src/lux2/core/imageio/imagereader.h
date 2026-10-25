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

#ifndef LUX2_IMAGEREADER_H
#define LUX2_IMAGEREADER_H

#include <cstdint>
#include <vector>

namespace lux2
{

    // Typed image buffer returned by the EXR/PNG readers.
    class ImageData
    {
    public:
        enum PixelDataType
        {
            UNSIGNED_CHAR_TYPE,
            UNSIGNED_SHORT_TYPE,
            FLOAT_TYPE
        };

        ImageData() : m_width(0), m_height(0), m_type(FLOAT_TYPE), m_noChannels(0)
        {
        }

        ImageData(int width, int height, PixelDataType type, int noChannels,
                  std::vector<std::uint8_t> &&data)
            : m_width(width), m_height(height), m_type(type),
              m_noChannels(noChannels), m_data(std::move(data))
        {
        }

        int Width() const { return m_width; }
        int Height() const { return m_height; }
        int Channels() const { return m_noChannels; }
        PixelDataType Type() const { return m_type; }

        std::size_t BytesPerChannel() const
        {
            switch (m_type)
            {
            case UNSIGNED_CHAR_TYPE:
                return 1;
            case UNSIGNED_SHORT_TYPE:
                return 2;
            case FLOAT_TYPE:
                return 4;
            }
            return 0;
        }

        const void *Data() const { return m_data.data(); }
        void *Data() { return m_data.data(); }

        // Pixel accessors, valid only when the type matches the accessor.
        unsigned char GetUChar(int x, int y, int c) const
        {
            return ChannelPtr<unsigned char>()[Index(x, y, c)];
        }
        unsigned short GetUShort(int x, int y, int c) const
        {
            return ChannelPtr<unsigned short>()[Index(x, y, c)];
        }
        float GetFloat(int x, int y, int c) const
        {
            return ChannelPtr<float>()[Index(x, y, c)];
        }

    private:
        std::size_t Index(int x, int y, int c) const
        {
            return (std::size_t(y) * m_width + x) * m_noChannels + c;
        }

        template <typename T>
        const T *ChannelPtr() const
        {
            return reinterpret_cast<const T *>(m_data.data());
        }

        int m_width, m_height;
        PixelDataType m_type;
        int m_noChannels;
        std::vector<std::uint8_t> m_data;
    };

} // namespace lux2

#endif // LUX2_IMAGEREADER_H
