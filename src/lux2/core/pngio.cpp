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

#include "core/pngio.h"

#include "core/colorsystem.h"
#include "core/error.h"
#include "core/math.h"

#include <png.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace lux2
{

    namespace
    {

        bool IsLittleEndian()
        {
            const std::uint16_t word = 0x0001;
            return *reinterpret_cast<const std::uint8_t *>(&word) == 0x01;
        }

    } // namespace

    bool WritePngImage(int channeltype, bool bit16, const std::string &name,
                       const std::vector<RGBColor> &pixels,
                       const std::vector<float> &alpha,
                       int xPixelCount, int yPixelCount,
                       int xResolution, int yResolution,
                       int xPixelStart, int yPixelStart,
                       const ColorSystem &cSystem, float screenGamma)
    {

        FILE *fp = fopen(name.c_str(), "wb");
        if (!fp)
        {
            LOG(LUX_SEVERE, LUX_SYSTEM)
                << "Cannot open PNG file '" << name << "' for output";
            return false;
        }

        png_structp png =
            png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
        if (!png)
        {
            fclose(fp);
            LOG(LUX_SEVERE, LUX_SYSTEM)
                << "Cannot create PNG write struct for '" << name << "'";
            return false;
        }
        png_infop info = png_create_info_struct(png);
        if (!info)
        {
            png_destroy_write_struct(&png, nullptr);
            fclose(fp);
            return false;
        }
        if (setjmp(png_jmpbuf(png)))
        {
            png_destroy_write_struct(&png, &info);
            fclose(fp);
            LOG(LUX_SEVERE, LUX_SYSTEM)
                << "Error writing PNG file '" << name << "'";
            return false;
        }
        png_init_io(png, fp);

        png_text text;
        memset(&text, 0, sizeof(text));
        text.compression = PNG_TEXT_COMPRESSION_NONE;
        text.key = (png_charp) "Software";
        text.text = (png_charp) "LuxRender2";
        png_set_text(png, info, &text, 1);

        png_color_16 black = {0, 0, 0, 0, 0};
        png_set_background(png, &black, PNG_BACKGROUND_GAMMA_SCREEN, 0, 255.0);

        // The requested gamma.
        const float gamma = 1.f / screenGamma;
        png_set_gAMA(png, info, gamma);

        png_set_cHRM(png, info, cSystem.xWhite, cSystem.yWhite, cSystem.xRed,
                     cSystem.yRed, cSystem.xGreen, cSystem.yGreen, cSystem.xBlue,
                     cSystem.yBlue);

        int colorType;
        switch (channeltype)
        {
        case 0:
            colorType = PNG_COLOR_TYPE_GRAY;
            break;
        case 1:
            colorType = PNG_COLOR_TYPE_GRAY_ALPHA;
            break;
        case 2:
            colorType = PNG_COLOR_TYPE_RGB;
            break;
        case 3:
            colorType = PNG_COLOR_TYPE_RGB_ALPHA;
            break;
        default:
            colorType = PNG_COLOR_TYPE_RGB;
        }

        png_set_IHDR(png, info, xResolution, yResolution, bit16 ? 16 : 8,
                     colorType, PNG_INTERLACE_NONE,
                     PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

        png_write_info(png, info);

        // PNG stores samples in network byte order.
        if (bit16 && IsLittleEndian())
            png_set_swap(png);

        const int channels = (colorType == PNG_COLOR_TYPE_GRAY)
                                 ? 1
                                 : (colorType == PNG_COLOR_TYPE_GRAY_ALPHA ? 2
                                                                           : (colorType == PNG_COLOR_TYPE_RGB ? 3 : 4));
        const bool gray = colorType == PNG_COLOR_TYPE_GRAY ||
                          colorType == PNG_COLOR_TYPE_GRAY_ALPHA;
        const bool hasAlpha = colorType == PNG_COLOR_TYPE_GRAY_ALPHA ||
                              colorType == PNG_COLOR_TYPE_RGB_ALPHA;

        // Write row by row.
        std::vector<png_uint_16> row16;
        std::vector<png_byte> row8;
        if (bit16)
            row16.assign(std::size_t(xResolution) * 4, 0);
        else
            row8.assign(std::size_t(xResolution) * 4, 0);

        auto writeZeroRow = [&]()
        {
            if (bit16)
                png_write_row(png, reinterpret_cast<png_bytep>(&row16[0]));
            else
                png_write_row(png, &row8[0]);
        };

        for (int y = 0; y < yPixelStart; ++y)
            writeZeroRow();

        for (int y = 0; y < yPixelCount; ++y)
        {
            int i = xPixelStart * channels;
            for (int x = 0; x < xPixelCount; ++x)
            {
                const std::size_t src = x + y * xPixelCount;
                if (bit16)
                {
                    if (gray)
                        row16[i++] = static_cast<png_uint_16>(
                            ClampF(65535.f * pixels[src].Y(), 0.f, 65535.f));
                    else
                    {
                        const RGBColor &c = pixels[src];
                        row16[i++] = static_cast<png_uint_16>(
                            ClampF(65535.f * c[0], 0.f, 65535.f));
                        row16[i++] = static_cast<png_uint_16>(
                            ClampF(65535.f * c[1], 0.f, 65535.f));
                        row16[i++] = static_cast<png_uint_16>(
                            ClampF(65535.f * c[2], 0.f, 65535.f));
                    }
                    if (hasAlpha)
                        row16[i++] = static_cast<png_uint_16>(
                            ClampF(65535.f * alpha[src], 0.f, 65535.f));
                }
                else
                {
                    if (gray)
                        row8[i++] = static_cast<png_byte>(
                            ClampF(255.f * pixels[src].Y(), 0.f, 255.f));
                    else
                    {
                        const RGBColor &c = pixels[src];
                        row8[i++] = static_cast<png_byte>(
                            ClampF(255.f * c[0], 0.f, 255.f));
                        row8[i++] = static_cast<png_byte>(
                            ClampF(255.f * c[1], 0.f, 255.f));
                        row8[i++] = static_cast<png_byte>(
                            ClampF(255.f * c[2], 0.f, 255.f));
                    }
                    if (hasAlpha)
                        row8[i++] = static_cast<png_byte>(
                            ClampF(255.f * alpha[src], 0.f, 255.f));
                }
            }
            writeZeroRow();
        }

        if (bit16)
            std::fill(row16.begin(), row16.end(), 0);
        else
            std::fill(row8.begin(), row8.end(), 0);
        for (int y = yPixelStart + yPixelCount; y < yResolution; ++y)
            writeZeroRow();

        png_write_end(png, info);
        png_destroy_write_struct(&png, &info);
        fclose(fp);
        return true;
    }

    bool ReadPngImage(const std::string &name, ImageData *out, float *gamma)
    {
        if (gamma)
            *gamma = 0.f;

        FILE *fp = fopen(name.c_str(), "rb");
        if (!fp)
        {
            LOG(LUX_ERROR, LUX_NOFILE)
                << "Unable to open image file '" << name << "'";
            return false;
        }

        // Signature check.
        png_byte sig[8];
        // png_sig_cmp returns 0 when the signature matches.
        if (fread(sig, 1, 8, fp) != 8 || png_sig_cmp(sig, 0, 8) != 0)
        {
            fclose(fp);
            LOG(LUX_ERROR, LUX_BADFILE)
                << "Cannot read image '" << name << "': not a PNG file";
            return false;
        }

        png_structp png =
            png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
        if (!png)
        {
            fclose(fp);
            return false;
        }
        png_infop info = png_create_info_struct(png);
        if (!info)
        {
            png_destroy_read_struct(&png, nullptr, nullptr);
            fclose(fp);
            return false;
        }
        if (setjmp(png_jmpbuf(png)))
        {
            png_destroy_read_struct(&png, &info, nullptr);
            fclose(fp);
            LOG(LUX_ERROR, LUX_BADFILE)
                << "Error reading image '" << name << "'";
            return false;
        }
        png_init_io(png, fp);
        png_set_sig_bytes(png, 8);

        png_read_info(png, info);

        png_uint_32 width = 0, height = 0;
        int bitDepth = 0, colorType = 0;
        png_get_IHDR(png, info, &width, &height, &bitDepth, &colorType, nullptr,
                     nullptr, nullptr);
        if (width == 0 || height == 0)
        {
            png_destroy_read_struct(&png, &info, nullptr);
            fclose(fp);
            LOG(LUX_ERROR, LUX_BADFILE)
                << "Empty PNG image '" << name << "'";
            return false;
        }

        double g = 0.;
        if (gamma && png_get_gAMA(png, info, &g))
            *gamma = float(g);

        // Expand palettes (and tRNS) to RGB(A), low-bit-depth gray to 8 bits.
        // Keep the native bit depth: 8 -> uchar, 16 -> ushort; 16-bit samples
        // are normalized to host byte order on little-endian machines.
        png_set_expand(png);
        if (bitDepth == 16 && IsLittleEndian())
            png_set_swap(png);
        png_read_update_info(png, info);

        png_get_IHDR(png, info, &width, &height, &bitDepth, &colorType, nullptr,
                     nullptr, nullptr);

        ImageData::PixelDataType type;
        std::size_t bytesPerSample = 0;
        if (bitDepth == 8)
        {
            type = ImageData::UNSIGNED_CHAR_TYPE;
            bytesPerSample = 1;
        }
        else if (bitDepth == 16)
        {
            type = ImageData::UNSIGNED_SHORT_TYPE;
            bytesPerSample = 2;
        }
        else
        {
            png_destroy_read_struct(&png, &info, nullptr);
            fclose(fp);
            LOG(LUX_ERROR, LUX_BADFILE)
                << "Unsupported bit depth " << bitDepth << " in PNG '" << name << "'";
            return false;
        }

        int channels = 0;
        switch (colorType)
        {
        case PNG_COLOR_TYPE_GRAY:
            channels = 1;
            break;
        case PNG_COLOR_TYPE_GRAY_ALPHA:
            channels = 2;
            break;
        case PNG_COLOR_TYPE_RGB:
            channels = 3;
            break;
        case PNG_COLOR_TYPE_RGB_ALPHA:
            channels = 4;
            break;
        default:
            png_destroy_read_struct(&png, &info, nullptr);
            fclose(fp);
            LOG(LUX_ERROR, LUX_BADFILE)
                << "Unsupported PNG color type " << colorType << " in '" << name << "'";
            return false;
        }

        std::vector<std::uint8_t> buffer(std::size_t(width) * height * channels *
                                         bytesPerSample);
        std::vector<png_bytep> rows(height);
        const std::size_t rowBytes = std::size_t(width) * channels * bytesPerSample;
        for (png_uint_32 y = 0; y < height; ++y)
            rows[y] = buffer.data() + y * rowBytes;

        png_read_image(png, rows.data());
        png_read_end(png, nullptr);
        png_destroy_read_struct(&png, &info, nullptr);
        fclose(fp);

        *out = ImageData(int(width), int(height), type, channels, std::move(buffer));
        return true;
    }

} // namespace lux2
