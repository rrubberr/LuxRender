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

#include "core/tiffio.h"

#include "core/colorsystem.h"
#include "core/error.h"
#include "core/math.h"

extern "C"
{
#include <tiffio.h>
}

#include <algorithm>
#include <csetjmp>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace lux2
{

    namespace
    {

        // libtiff aborts on fatal errors by default, fail gracefully.
        struct TiffErrorManager
        {
            jmp_buf jumpBuffer;
        };

        thread_local TiffErrorManager *g_tiffError = nullptr;

        void TiffErrorHandler(const char *, const char *, va_list)
        {
            if (g_tiffError)
                longjmp(g_tiffError->jumpBuffer, 1);
        }

        class TiffErrorScope
        {
        public:
            TiffErrorScope() : m_installed(false)
            {
            }
            ~TiffErrorScope()
            {
                Remove();
            }

            void Install()
            {
                m_oldHandler = TIFFSetErrorHandler(TiffErrorHandler);
                TIFFSetWarningHandler(nullptr);
                g_tiffError = &m_manager;
                m_installed = true;
            }

            TiffErrorManager *Manager() { return &m_manager; }

            void Remove()
            {
                if (m_installed)
                {
                    TIFFSetErrorHandler(m_oldHandler);
                    if (g_tiffError == &m_manager)
                        g_tiffError = nullptr;
                    m_installed = false;
                }
            }

        private:
            TiffErrorManager m_manager;
            TIFFErrorHandler m_oldHandler;
            bool m_installed;
        };

        // Exact rational form of a float with a fixed denominator.
        inline uint32 ToRationalNum(float v, uint32 den)
        {
            return uint32(std::lround(v * float(den)));
        }

    } // namespace

    bool WriteTiffImage(int channeltype, int bitdepth, int compressiontype,
                        const std::string &name,
                        const std::vector<RGBColor> &pixels,
                        const std::vector<float> &alpha,
                        int xPixelCount, int yPixelCount,
                        int xResolution, int yResolution,
                        int xPixelStart, int yPixelStart,
                        const ColorSystem &cSystem, float screenGamma)
    {
        if (bitdepth != 8 && bitdepth != 16 && bitdepth != 32)
        {
            LOG(LUX_SEVERE, LUX_ILLSTATE)
                << "Unsupported TIFF bit depth " << bitdepth;
            return false;
        }

        const bool isFloat = bitdepth == 32;
        // Channel types: 0=Y, 1=YA, 2=RGB, 3=RGBA.
        const bool gray = channeltype <= 1;
        const bool hasAlpha = (channeltype == 1) || (channeltype == 3);
        const int channels = (gray ? 1 : 3) + (hasAlpha ? 1 : 0);

        TiffErrorScope errScope;
        errScope.Install();
        if (setjmp(errScope.Manager()->jumpBuffer))
        {
            LOG(LUX_SEVERE, LUX_SYSTEM)
                << "Error writing TIFF file '" << name << "'";
            return false;
        }

        TIFF *tif = TIFFOpen(name.c_str(), "w");
        if (!tif)
        {
            LOG(LUX_SEVERE, LUX_SYSTEM)
                << "Cannot open TIFF file '" << name << "' for output";
            return false;
        }

        TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, uint32(xResolution));
        TIFFSetField(tif, TIFFTAG_IMAGELENGTH, uint32(yResolution));
        TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, uint16(channels));
        TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, uint16(bitdepth));
        TIFFSetField(tif, TIFFTAG_SAMPLEFORMAT,
                     isFloat ? SAMPLEFORMAT_IEEEFP : SAMPLEFORMAT_UINT);
        // Sample values are black=0/white=max.
        TIFFSetField(tif, TIFFTAG_PHOTOMETRIC,
                     gray ? PHOTOMETRIC_MINISBLACK : PHOTOMETRIC_RGB);
        if (hasAlpha)
        {
            uint16 extra = EXTRASAMPLE_UNASSALPHA;
            TIFFSetField(tif, TIFFTAG_EXTRASAMPLES, uint16(1), &extra);
        }
        TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
        TIFFSetField(tif, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
        switch (compressiontype)
        {
        case TIFF_COMPRESSION_LZW:
            TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_LZW);
            break;
        case TIFF_COMPRESSION_DEFLATE:
            TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_ADOBE_DEFLATE);
            break;
        default:
            TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
            break;
        }

        const char *software = "LuxRender2";
        TIFFSetField(tif, TIFFTAG_SOFTWARE, software);
        TIFFSetField(tif, TIFFTAG_RESOLUTIONUNIT, RESUNIT_INCH);
        {
            float xres = 72.f, yres = 72.f;
            TIFFSetField(tif, TIFFTAG_XRESOLUTION, xres);
            TIFFSetField(tif, TIFFTAG_YRESOLUTION, yres);
        }

        // Colorimetry.
        (void)screenGamma;
        {
            const uint32 den = 100000;
            uint32 primaries[6] = {
                ToRationalNum(cSystem.xRed, den),
                ToRationalNum(cSystem.yRed, den),
                ToRationalNum(cSystem.xGreen, den),
                ToRationalNum(cSystem.yGreen, den),
                ToRationalNum(cSystem.xBlue, den),
                ToRationalNum(cSystem.yBlue, den)};
            TIFFSetField(tif, TIFFTAG_PRIMARYCHROMATICITIES, primaries);
            uint32 white[2] = {ToRationalNum(cSystem.xWhite, den),
                               ToRationalNum(cSystem.yWhite, den)};
            TIFFSetField(tif, TIFFTAG_WHITEPOINT, white);
        }

        const std::size_t elemSize = isFloat ? sizeof(float)
                                             : (bitdepth == 16 ? sizeof(uint16)
                                                               : sizeof(uint8));
        std::vector<std::uint8_t> row(std::size_t(xResolution) * channels *
                                      elemSize);
        const float scale = (bitdepth == 16) ? 65535.f : 255.f;

        auto writeRow = [&](int y)
        {
            if (TIFFWriteScanline(tif, row.data(), tmsize_t(y), 0) < 0)
            {
                LOG(LUX_SEVERE, LUX_SYSTEM)
                    << "Error writing TIFF scanline " << y << " in '" << name << "'";
                longjmp(errScope.Manager()->jumpBuffer, 1);
            }
        };

        auto fillRow = [&](int y)
        {
            std::fill(row.begin(), row.end(), std::uint8_t(0));
            const int srcY = y - yPixelStart;
            for (int x = xPixelStart; x < xPixelStart + xPixelCount; ++x)
            {
                if (x < 0 || x >= xResolution)
                    continue;
                const std::size_t src =
                    std::size_t(srcY) * xPixelCount + (x - xPixelStart);
                const RGBColor &c = pixels[src];
                std::uint8_t *dst =
                    row.data() + std::size_t(x) * channels * elemSize;
                float values[4];
                values[0] = gray ? c.Y() : c[0];
                if (!gray)
                {
                    values[1] = c[1];
                    values[2] = c[2];
                }
                if (hasAlpha)
                    values[channels - 1] = alpha[src];
                for (int ch = 0; ch < channels; ++ch)
                {
                    if (isFloat)
                        reinterpret_cast<float *>(dst)[ch] = values[ch];
                    else if (bitdepth == 16)
                        reinterpret_cast<uint16 *>(dst)[ch] = uint16(
                            std::lround(ClampF(scale * values[ch], 0.f, scale)));
                    else
                        reinterpret_cast<uint8 *>(dst)[ch] = uint8(std::lround(
                            ClampF(scale * values[ch], 0.f, scale)));
                }
            }
        };

        for (int y = 0; y < yResolution; ++y)
        {
            const bool inBand = y >= yPixelStart && y < yPixelStart + yPixelCount;
            if (inBand)
                fillRow(y);
            else
                std::fill(row.begin(), row.end(), std::uint8_t(0));
            writeRow(y);
        }

        TIFFWriteDirectory(tif);
        TIFFClose(tif);
        return true;
    }

    bool ReadTiffImage(const std::string &name, ImageData *out)
    {
        TiffErrorScope errScope;
        errScope.Install();
        if (setjmp(errScope.Manager()->jumpBuffer))
        {
            LOG(LUX_ERROR, LUX_BADFILE)
                << "Cannot read image '" << name << "': not a TIFF file";
            return false;
        }

        TIFF *tif = TIFFOpen(name.c_str(), "r");
        if (!tif)
        {
            LOG(LUX_ERROR, LUX_NOFILE)
                << "Unable to open image file '" << name << "'";
            return false;
        }

        uint32 width = 0, height = 0;
        uint16 samplesPerPixel = 0, bitsPerSample = 0, sampleFormat = SAMPLEFORMAT_UINT;
        uint16 planarConfig = PLANARCONFIG_CONTIG;
        TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width);
        TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height);
        TIFFGetField(tif, TIFFTAG_SAMPLESPERPIXEL, &samplesPerPixel);
        TIFFGetFieldDefaulted(tif, TIFFTAG_BITSPERSAMPLE, &bitsPerSample);
        TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLEFORMAT, &sampleFormat);
        TIFFGetFieldDefaulted(tif, TIFFTAG_PLANARCONFIG, &planarConfig);

        ImageData::PixelDataType type;
        std::size_t elemSize = 0;
        if (bitsPerSample == 8 && sampleFormat == SAMPLEFORMAT_UINT)
        {
            type = ImageData::UNSIGNED_CHAR_TYPE;
            elemSize = 1;
        }
        else if (bitsPerSample == 16 && sampleFormat == SAMPLEFORMAT_UINT)
        {
            type = ImageData::UNSIGNED_SHORT_TYPE;
            elemSize = 2;
        }
        else if (bitsPerSample == 32 && sampleFormat == SAMPLEFORMAT_IEEEFP)
        {
            type = ImageData::FLOAT_TYPE;
            elemSize = 4;
        }
        else
        {
            TIFFClose(tif);
            LOG(LUX_ERROR, LUX_BADFILE)
                << "Unsupported TIFF sample format in '" << name << "' ("
                << bitsPerSample << " bits, format " << sampleFormat << ")";
            return false;
        }

        if (width == 0 || height == 0 || samplesPerPixel < 1 ||
            samplesPerPixel > 4)
        {
            TIFFClose(tif);
            LOG(LUX_ERROR, LUX_BADFILE)
                << "Unsupported TIFF layout in '" << name << "' (" << width
                << "x" << height << ", " << samplesPerPixel << " samples)";
            return false;
        }

        const int channels = int(samplesPerPixel);
        std::vector<std::uint8_t> buffer(std::size_t(width) * height * channels *
                                         elemSize);
        std::vector<std::uint8_t> scanline(
            std::size_t(TIFFScanlineSize(tif)));

        if (planarConfig == PLANARCONFIG_CONTIG)
        {
            for (uint32 y = 0; y < height; ++y)
            {
                if (TIFFReadScanline(tif, scanline.data(), tmsize_t(y), 0) < 0)
                {
                    TIFFClose(tif);
                    LOG(LUX_ERROR, LUX_BADFILE)
                        << "Error reading TIFF scanline " << y << " in '" << name
                        << "'";
                    return false;
                }
                std::memcpy(buffer.data() + std::size_t(y) * channels * elemSize * width,
                            scanline.data(),
                            std::size_t(width) * channels * elemSize);
            }
        }
        else
        {
            // Read each sample plane and interleave.
            for (uint16 s = 0; s < samplesPerPixel; ++s)
            {
                for (uint32 y = 0; y < height; ++y)
                {
                    if (TIFFReadScanline(tif, scanline.data(), tmsize_t(y), s) < 0)
                    {
                        TIFFClose(tif);
                        LOG(LUX_ERROR, LUX_BADFILE)
                            << "Error reading TIFF plane " << s << " scanline " << y
                            << " in '" << name << "'";
                        return false;
                    }
                    const std::uint8_t *src = scanline.data();
                    std::uint8_t *dstBase = buffer.data() +
                                            std::size_t(y) * width * channels * elemSize +
                                            std::size_t(s) * elemSize;
                    for (uint32 x = 0; x < width; ++x)
                        std::memcpy(dstBase + std::size_t(x) * channels * elemSize,
                                    src + std::size_t(x) * elemSize, elemSize);
                }
            }
        }

        TIFFClose(tif);

        *out = ImageData(int(width), int(height), type, channels, std::move(buffer));
        return true;
    }

} // namespace lux2
