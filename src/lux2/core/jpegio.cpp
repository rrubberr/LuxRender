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

#include "core/jpegio.h"

#include "core/error.h"
#include "core/math.h"

extern "C"
{
#include <jpeglib.h>
#include <jerror.h>
}

#include <algorithm>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace lux2
{

    namespace
    {

        // libjpeg reports fatal errors with longjmp.
        struct JpegErrorManager
        {
            jpeg_error_mgr pub;
            jmp_buf jumpBuffer;
        };

        void JpegErrorExit(j_common_ptr cinfo)
        {
            JpegErrorManager *err = reinterpret_cast<JpegErrorManager *>(cinfo->err);
            longjmp(err->jumpBuffer, 1);
        }

        void JpegOutputMessage(j_common_ptr /*cinfo*/)
        {
            // Handle warnings.
        }

        void InitErrorManager(jpeg_error_mgr *err)
        {
            jpeg_std_error(err);
            err->error_exit = JpegErrorExit;
            err->output_message = JpegOutputMessage;
        }

    } // namespace

    bool WriteJpegImage(int channeltype, int quality, bool subsampling,
                        const std::string &name,
                        const std::vector<RGBColor> &pixels,
                        const std::vector<float> &alpha,
                        int xPixelCount, int yPixelCount,
                        int xResolution, int yResolution,
                        int xPixelStart, int yPixelStart)
    {
        // JPEG has no alpha channel.
        if (channeltype == 1 || channeltype == 3)
        {
            LOG(LUX_WARNING, LUX_ILLSTATE)
                << "JPEG does not support alpha channels, writing '" << name
                << "' without alpha";
            channeltype = (channeltype == 1) ? 0 : 2;
        }
        const bool gray = channeltype <= 0;
        const int outChannels = gray ? 1 : 3;

        quality = ClampF(float(quality), 1.f, 100.f);

        FILE *fp = fopen(name.c_str(), "wb");
        if (!fp)
        {
            LOG(LUX_SEVERE, LUX_SYSTEM)
                << "Cannot open JPEG file '" << name << "' for output";
            return false;
        }

        struct jpeg_compress_struct cinfo;
        JpegErrorManager jerr;
        cinfo.err = &jerr.pub;
        InitErrorManager(cinfo.err);

        if (setjmp(jerr.jumpBuffer))
        {
            jpeg_destroy_compress(&cinfo);
            fclose(fp);
            LOG(LUX_SEVERE, LUX_SYSTEM)
                << "Error writing JPEG file '" << name << "'";
            return false;
        }

        jpeg_create_compress(&cinfo);
        jpeg_stdio_dest(&cinfo, fp);

        cinfo.image_width = JDIMENSION(xResolution);
        cinfo.image_height = JDIMENSION(yResolution);
        cinfo.input_components = outChannels;
        cinfo.in_color_space = gray ? JCS_GRAYSCALE : JCS_RGB;
        jpeg_set_defaults(&cinfo);
        jpeg_set_quality(&cinfo, quality, TRUE);
        if (!subsampling)
        {
            for (int i = 0; i < cinfo.num_components; ++i)
            {
                cinfo.comp_info[i].h_samp_factor = 1;
                cinfo.comp_info[i].v_samp_factor = 1;
            }
        }
        jpeg_start_compress(&cinfo, TRUE);

        // Rows outside the crop band are written black.
        std::vector<JSAMPLE> row(std::size_t(xResolution) * outChannels, 0);
        JSAMPROW rowPointer[1] = {row.data()};

        while (cinfo.next_scanline < cinfo.image_height)
        {
            const int y = int(cinfo.next_scanline);
            const bool inBand = y >= yPixelStart && y < yPixelStart + yPixelCount;
            if (inBand)
            {
                std::fill(row.begin(), row.end(), JSAMPLE(0));
                const int srcY = y - yPixelStart;
                for (int x = xPixelStart; x < xPixelStart + xPixelCount; ++x)
                {
                    if (x < 0 || x >= xResolution)
                        continue;
                    const std::size_t src =
                        std::size_t(srcY) * xPixelCount + (x - xPixelStart);
                    const RGBColor &c = pixels[src];
                    JSAMPLE *dst = &row[std::size_t(x) * outChannels];
                    if (gray)
                        dst[0] = JSAMPLE(ClampF(255.f * c.Y(), 0.f, 255.f));
                    else
                    {
                        dst[0] = JSAMPLE(ClampF(255.f * c[0], 0.f, 255.f));
                        dst[1] = JSAMPLE(ClampF(255.f * c[1], 0.f, 255.f));
                        dst[2] = JSAMPLE(ClampF(255.f * c[2], 0.f, 255.f));
                    }
                }
            }
            else
                std::fill(row.begin(), row.end(), JSAMPLE(0));
            jpeg_write_scanlines(&cinfo, rowPointer, 1);
        }

        jpeg_finish_compress(&cinfo);
        jpeg_destroy_compress(&cinfo);
        fclose(fp);
        return true;
    }

    bool ReadJpegImage(const std::string &name, ImageData *out)
    {
        FILE *fp = fopen(name.c_str(), "rb");
        if (!fp)
        {
            LOG(LUX_ERROR, LUX_NOFILE)
                << "Unable to open image file '" << name << "'";
            return false;
        }

        struct jpeg_decompress_struct cinfo;
        JpegErrorManager jerr;
        cinfo.err = &jerr.pub;
        InitErrorManager(cinfo.err);

        if (setjmp(jerr.jumpBuffer))
        {
            jpeg_destroy_decompress(&cinfo);
            fclose(fp);
            LOG(LUX_ERROR, LUX_BADFILE)
                << "Error reading JPEG image '" << name << "'";
            return false;
        }

        jpeg_create_decompress(&cinfo);
        jpeg_stdio_src(&cinfo, fp);

        // Not a JPEG.
        if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK)
        {
            jpeg_destroy_decompress(&cinfo);
            fclose(fp);
            LOG(LUX_ERROR, LUX_BADFILE)
                << "Cannot read image '" << name << "': not a JPEG file";
            return false;
        }

        // Grayscale stays 1-channel, everything else to 3-channel RGB.
        cinfo.out_color_space = (cinfo.jpeg_color_space == JCS_GRAYSCALE)
                                    ? JCS_GRAYSCALE
                                    : JCS_RGB;
        jpeg_start_decompress(&cinfo);

        const int width = int(cinfo.output_width);
        const int height = int(cinfo.output_height);
        const int channels = int(cinfo.output_components);
        if (width <= 0 || height <= 0 || (channels != 1 && channels != 3))
        {
            jpeg_destroy_decompress(&cinfo);
            fclose(fp);
            LOG(LUX_ERROR, LUX_BADFILE)
                << "Unsupported JPEG format in '" << name << "' (" << channels
                << " components)";
            return false;
        }

        std::vector<std::uint8_t> buffer(std::size_t(width) * height * channels);
        JSAMPROW rowPointer[1];
        while (cinfo.output_scanline < cinfo.output_height)
        {
            rowPointer[0] = buffer.data() +
                            std::size_t(cinfo.output_scanline) * width * channels;
            jpeg_read_scanlines(&cinfo, rowPointer, 1);
        }

        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);
        fclose(fp);

        *out = ImageData(width, height, ImageData::UNSIGNED_CHAR_TYPE, channels,
                         std::move(buffer));
        return true;
    }

} // namespace lux2
