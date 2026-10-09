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

#include "core/exrio.h"

#include "core/error.h"

#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfFrameBuffer.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfInputFile.h>
#include <OpenEXR/ImfOutputFile.h>
#include <Imath/ImathBox.h>
#include <Imath/half.h>

#include <cstring>
#include <memory>

using namespace Imf;
using namespace Imath;

namespace lux2
{

    bool WriteOpenEXRImage(int channeltype, bool halftype, int compressiontype,
                           const std::string &name,
                           const std::vector<RGBColor> &pixels,
                           const std::vector<float> &alpha,
                           int xRes, int yRes,
                           int totalXRes, int totalYRes,
                           int xOffset, int yOffset)
    {
        Header header(totalXRes, totalYRes);

        // Set compression.
        switch (compressiontype)
        {
        case 0:
            header.compression() = RLE_COMPRESSION;
            break;
        case 1:
            header.compression() = PIZ_COMPRESSION;
            break;
        case 2:
            header.compression() = ZIP_COMPRESSION;
            break;
        case 3:
            header.compression() = PXR24_COMPRESSION;
            break;
        case 4:
            header.compression() = NO_COMPRESSION;
            break;
        default:
            header.compression() = RLE_COMPRESSION;
            break;
        }

        Box2i dataWindow(V2i(xOffset, yOffset),
                         V2i(xOffset + xRes - 1, yOffset + yRes - 1));
        header.dataWindow() = dataWindow;

        const PixelType savetype = halftype ? HALF : FLOAT;

        // Define channels: 0=Y, 1=YA, 2=RGB, 3=RGBA.
        if (channeltype == 0)
        {
            header.channels().insert("Y", Channel(savetype));
        }
        else if (channeltype == 1)
        {
            header.channels().insert("Y", Channel(savetype));
            header.channels().insert("A", Channel(savetype));
        }
        else if (channeltype == 2)
        {
            header.channels().insert("R", Channel(savetype));
            header.channels().insert("G", Channel(savetype));
            header.channels().insert("B", Channel(savetype));
        }
        else
        {
            header.channels().insert("R", Channel(savetype));
            header.channels().insert("G", Channel(savetype));
            header.channels().insert("B", Channel(savetype));
            header.channels().insert("A", Channel(savetype));
        }

        FrameBuffer fb;

        // Staging buffers for type conversion.
        const std::size_t bufSize = std::size_t(xRes) * yRes;
        const std::size_t bufOffset = std::size_t(xOffset) + std::size_t(yOffset) * xRes;

        std::vector<float> fy;
        std::vector<Imath::half> hy, hrgb, ha;

        if (!halftype)
        {
            // 32bit FLOAT output.
            if (channeltype <= 1)
            {
                // Save Y.
                fy.resize(bufSize);
                for (std::size_t i = 0; i < bufSize; ++i)
                    fy[i] = (0.3f * pixels[i][0]) + (0.59f * pixels[i][1]) +
                            (0.11f * pixels[i][2]);
                fb.insert("Y", Slice(FLOAT,
                                     (char *)(&fy[0] - bufOffset), sizeof(float),
                                     xRes * sizeof(float)));
            }
            else if (channeltype >= 2)
            {
                // Save RGB straight from the interleaved pixel buffer.
                // Enoki RGBColor is padded to 16 bytes.
                char *base = reinterpret_cast<char *>(
                                 const_cast<RGBColor *>(&pixels[0])) -
                             bufOffset * sizeof(RGBColor);
                fb.insert("R", Slice(FLOAT, base,
                                     sizeof(RGBColor), xRes * sizeof(RGBColor)));
                fb.insert("G", Slice(FLOAT, base + sizeof(float),
                                     sizeof(RGBColor), xRes * sizeof(RGBColor)));
                fb.insert("B", Slice(FLOAT, base + 2 * sizeof(float),
                                     sizeof(RGBColor), xRes * sizeof(RGBColor)));
            }
            if (channeltype == 1 || channeltype == 3)
            {
                float *fa = const_cast<float *>(&alpha[0]);
                fb.insert("A", Slice(FLOAT,
                                     (char *)(fa - bufOffset), sizeof(float),
                                     xRes * sizeof(float)));
            }
        }
        else
        {
            // 16bit HALF output.
            if (channeltype <= 1)
            {
                hy.resize(bufSize);
                for (std::size_t i = 0; i < bufSize; ++i)
                    hy[i] = (0.3f * pixels[i][0]) + (0.59f * pixels[i][1]) +
                            (0.11f * pixels[i][2]);
                fb.insert("Y", Slice(HALF, (char *)(&hy[0] - bufOffset),
                                     sizeof(Imath::half),
                                     xRes * sizeof(Imath::half)));
            }
            else if (channeltype >= 2)
            {
                hrgb.resize(3 * bufSize);
                for (std::size_t i = 0; i < bufSize; ++i)
                    for (int j = 0; j < 3; j++)
                        hrgb[3 * i + j] = pixels[i][j];
                fb.insert("R", Slice(HALF,
                                     (char *)(hrgb.data() - 3 * bufOffset),
                                     3 * sizeof(Imath::half),
                                     xRes * (3 * sizeof(Imath::half))));
                fb.insert("G", Slice(HALF,
                                     (char *)(hrgb.data() - 3 * bufOffset) + sizeof(Imath::half),
                                     3 * sizeof(Imath::half),
                                     xRes * (3 * sizeof(Imath::half))));
                fb.insert("B", Slice(HALF,
                                     (char *)(hrgb.data() - 3 * bufOffset) + 2 * sizeof(Imath::half),
                                     3 * sizeof(Imath::half),
                                     xRes * (3 * sizeof(Imath::half))));
            }
            if (channeltype == 1 || channeltype == 3)
            {
                ha.resize(bufSize);
                for (std::size_t i = 0; i < bufSize; ++i)
                    ha[i] = alpha[i];
                fb.insert("A", Slice(HALF, (char *)(&ha[0] - bufOffset),
                                     sizeof(Imath::half),
                                     xRes * sizeof(Imath::half)));
            }
        }

        bool result = true;
        try
        {
            OutputFile file(name.c_str(), header);
            file.setFrameBuffer(fb);
            file.writePixels(yRes);
        }
        catch (const std::exception &e)
        {
            LOG(LUX_SEVERE, LUX_BUG)
                << "Unable to write image file '" << name << "': " << e.what();
            result = false;
        }

        return result;
    }

    bool WriteOpenEXRChannels(bool halftype, int compressiontype,
                              const std::string &name,
                              const std::vector<EXRChannel> &channels,
                              int xRes, int yRes,
                              int totalXRes, int totalYRes,
                              int xOffset, int yOffset)
    {
        if (channels.empty())
            return false;

        Header header(totalXRes, totalYRes);
        switch (compressiontype)
        {
        case 0: header.compression() = RLE_COMPRESSION; break;
        case 1: header.compression() = PIZ_COMPRESSION; break;
        case 2: header.compression() = ZIP_COMPRESSION; break;
        case 3: header.compression() = PXR24_COMPRESSION; break;
        case 4: header.compression() = NO_COMPRESSION; break;
        default: header.compression() = RLE_COMPRESSION; break;
        }

        Box2i dataWindow(V2i(xOffset, yOffset),
                         V2i(xOffset + xRes - 1, yOffset + yRes - 1));
        header.dataWindow() = dataWindow;

        const PixelType savetype = halftype ? HALF : FLOAT;
        const std::size_t bufSize = std::size_t(xRes) * yRes;
        const std::size_t bufOffset =
            std::size_t(xOffset) + std::size_t(yOffset) * xRes;

        // Every HALF channel needs its own staging buffer until writePixels().
        std::vector<std::vector<Imath::half>> halfStaging;
        if (halftype)
            halfStaging.resize(channels.size());

        FrameBuffer fb;
        for (std::size_t c = 0; c < channels.size(); ++c)
        {
            const EXRChannel &ch = channels[c];
            header.channels().insert(ch.name, Channel(savetype));
            if (halftype)
            {
                std::vector<Imath::half> &hb = halfStaging[c];
                hb.resize(bufSize);
                for (std::size_t i = 0; i < bufSize; ++i)
                    hb[i] = ch.pixels[i];
                fb.insert(ch.name,
                          Slice(HALF,
                                (char *)(&hb[0] - bufOffset),
                                sizeof(Imath::half),
                                xRes * sizeof(Imath::half)));
            }
            else
            {
                // Point the slice at the caller's plane.
                fb.insert(ch.name,
                          Slice(FLOAT,
                                (char *)(const_cast<float *>(ch.pixels) -
                                         bufOffset),
                                sizeof(float), xRes * sizeof(float)));
            }
        }

        bool result = true;
        try
        {
            OutputFile file(name.c_str(), header);
            file.setFrameBuffer(fb);
            file.writePixels(yRes);
        }
        catch (const std::exception &e)
        {
            LOG(LUX_SEVERE, LUX_BUG)
                << "Unable to write multi-channel image file '" << name
                << "': " << e.what();
            result = false;
        }
        return result;
    }

    namespace
    {

        // Native-typed staging buffer for one input channel.
        struct ReadChannel
        {
            std::string name;
            PixelType type;
            std::vector<char> data; // xRes*yRes of the native type
        };

        // Convert a native-typed channel.
        void ConvertToFloat(const ReadChannel &ch, std::size_t count,
                            int nch, int c, float *dst)
        {
            if (ch.type == FLOAT)
            {
                const float *src = reinterpret_cast<const float *>(ch.data.data());
                for (std::size_t i = 0; i < count; ++i)
                    dst[i * nch + c] = src[i];
            }
            else
            {
                const Imath::half *src =
                    reinterpret_cast<const Imath::half *>(ch.data.data());
                for (std::size_t i = 0; i < count; ++i)
                    dst[i * nch + c] = src[i];
            }
        }

    } // namespace

    bool ReadOpenEXRImage(const std::string &name, ImageData *out)
    {
        try
        {
            InputFile file(name.c_str());
            const Header &header = file.header();
            const Box2i &window = header.dataWindow();

            const int xMin = window.min.x, yMin = window.min.y;
            const int xRes = window.max.x - xMin + 1;
            const int yRes = window.max.y - yMin + 1;
            if (xRes <= 0 || yRes <= 0)
            {
                LOG(LUX_ERROR, LUX_BADFILE)
                    << "EXR file '" << name << "' has an empty dataWindow";
                return false;
            }

            const ChannelList &channels = header.channels();

            auto hasChannel = [&channels](const char *name)
            {
                return channels.find(name) != channels.end();
            };

            // First-match channel selection: RGB(A) preferred, then Y(A).
            std::vector<std::string> wanted;
            const bool hasRGB = hasChannel("R") && hasChannel("G") && hasChannel("B");
            if (hasRGB)
            {
                wanted = {"R", "G", "B"};
                if (hasChannel("A"))
                    wanted.push_back("A");
            }
            else if (hasChannel("Y"))
            {
                wanted = {"Y"};
                if (hasChannel("A"))
                    wanted.push_back("A");
            }
            else
            {
                LOG(LUX_ERROR, LUX_BADFILE)
                    << "Unsupported channels in EXR file '" << name << "'";
                return false;
            }

            std::vector<ReadChannel> rcs(wanted.size());
            FrameBuffer fb;
            const std::size_t count = std::size_t(xRes) * yRes;

            for (std::size_t c = 0; c < wanted.size(); ++c)
            {
                ReadChannel &rc = rcs[c];
                rc.name = wanted[c];
                const Channel &channel = channels[rc.name.c_str()];
                if (channel.type != FLOAT && channel.type != HALF)
                {
                    LOG(LUX_ERROR, LUX_BADFILE)
                        << "Unsupported pixel type for channel '" << rc.name
                        << "' in EXR file '" << name << "'";
                    return false;
                }
                rc.type = channel.type;
                const std::size_t elemSize =
                    (rc.type == FLOAT) ? sizeof(float) : sizeof(Imath::half);
                rc.data.resize(count * elemSize);

                // Point the slice so that window pixel (x, y) lands on
                // data[(x - xMin) + (y - yMin) * xRes].
                const std::ptrdiff_t base =
                    std::ptrdiff_t(xMin) * std::ptrdiff_t(elemSize) +
                    std::ptrdiff_t(yMin) * xRes * std::ptrdiff_t(elemSize);
                fb.insert(rc.name,
                          Slice(rc.type, (char *)rc.data.data() - base,
                                elemSize, xRes * elemSize));
            }

            file.setFrameBuffer(fb);
            file.readPixels(yMin, window.max.y);

            const int nch = int(wanted.size());
            std::vector<std::uint8_t> buffer(count * nch * sizeof(float));
            float *dst = reinterpret_cast<float *>(buffer.data());
            for (int c = 0; c < nch; ++c)
                ConvertToFloat(rcs[c], count, nch, c, dst);

            *out = ImageData(xRes, yRes, ImageData::FLOAT_TYPE, nch,
                             std::move(buffer));
            return true;
        }
        catch (const std::exception &e)
        {
            LOG(LUX_ERROR, LUX_BUG)
                << "Unable to read image file '" << name << "': " << e.what();
            return false;
        }
    }

    bool ListOpenEXRChannels(const std::string &name,
                             std::vector<std::string> *out)
    {
        try
        {
            InputFile file(name.c_str());
            out->clear();
            for (ChannelList::ConstIterator it = file.header().channels().begin();
                 it != file.header().channels().end(); ++it)
                out->push_back(it.name());
            return true;
        }
        catch (const std::exception &e)
        {
            LOG(LUX_ERROR, LUX_BUG)
                << "Unable to read image file '" << name << "': " << e.what();
            return false;
        }
    }

    bool ReadOpenEXRChannel(const std::string &name,
                            const std::string &channel,
                            std::vector<float> *out)
    {
        try
        {
            InputFile file(name.c_str());
            const Header &header = file.header();
            const Box2i &window = header.dataWindow();
            const int xMin = window.min.x, yMin = window.min.y;
            const int xRes = window.max.x - xMin + 1;
            const int yRes = window.max.y - yMin + 1;
            if (xRes <= 0 || yRes <= 0)
                return false;

            const ChannelList &channels = header.channels();
            if (channels.find(channel.c_str()) == channels.end())
                return false;
            const Channel &ch = channels[channel.c_str()];
            if (ch.type != FLOAT && ch.type != HALF)
                return false;

            const std::size_t count = std::size_t(xRes) * yRes;
            const std::size_t elemSize =
                (ch.type == FLOAT) ? sizeof(float) : sizeof(Imath::half);
            std::vector<char> data(count * elemSize);
            const std::ptrdiff_t base =
                std::ptrdiff_t(xMin) * std::ptrdiff_t(elemSize) +
                std::ptrdiff_t(yMin) * xRes * std::ptrdiff_t(elemSize);
            FrameBuffer fb;
            fb.insert(channel, Slice(ch.type, (char *)data.data() - base,
                                     elemSize, xRes * elemSize));
            file.setFrameBuffer(fb);
            file.readPixels(yMin, window.max.y);

            out->resize(count);
            if (ch.type == FLOAT)
            {
                const float *src = reinterpret_cast<const float *>(data.data());
                for (std::size_t i = 0; i < count; ++i)
                    (*out)[i] = src[i];
            }
            else
            {
                const Imath::half *src =
                    reinterpret_cast<const Imath::half *>(data.data());
                for (std::size_t i = 0; i < count; ++i)
                    (*out)[i] = float(src[i]);
            }
            return true;
        }
        catch (const std::exception &e)
        {
            LOG(LUX_ERROR, LUX_BUG)
                << "Unable to read image file '" << name << "': " << e.what();
            return false;
        }
    }

} // namespace lux2
