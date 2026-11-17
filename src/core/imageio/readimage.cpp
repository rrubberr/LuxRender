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

#include "core/imageio/readimage.h"

#include "core/error.h"
#include "core/imageio/exrio.h"
#include "core/imageio/jpegio.h"
#include "core/imageio/pngio.h"
#include "core/imageio/tiffio.h"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <unordered_map>

namespace lux2
{
    namespace
    {
        std::string ToLower(const std::string &s)
        {
            std::string r = s;
            std::transform(r.begin(), r.end(), r.begin(),
                           [](unsigned char c)
                           { return std::tolower(c); });
            return r;
        }

        std::string Extension(const std::string &name)
        {
            const size_t dot = name.find_last_of('.');
            if (dot == std::string::npos)
                return "";
            return ToLower(name.substr(dot + 1));
        }

        // Read a typed pixel as float, normalizing integer ranges.
        float PixelFloat(const ImageData &img, int x, int y, int c)
        {
            switch (img.Type())
            {
            case ImageData::UNSIGNED_CHAR_TYPE:
                return img.GetUChar(x, y, c) * (1.f / 255.f);
            case ImageData::UNSIGNED_SHORT_TYPE:
                return img.GetUShort(x, y, c) * (1.f / 65535.f);
            case ImageData::FLOAT_TYPE:
            default:
                return img.GetFloat(x, y, c);
            }
        }
    } // namespace

    bool ReadImageByExtension(const std::string &name, ImageData *out)
    {
        const std::string ext = Extension(name);
        if (ext == "exr")
            return ReadOpenEXRImage(name, out);
        if (ext == "png")
            return ReadPngImage(name, out);
        if (ext == "jpg" || ext == "jpeg")
            return ReadJpegImage(name, out);
        if (ext == "tif" || ext == "tiff")
            return ReadTiffImage(name, out);

        LOG(LUX_ERROR, LUX_BADFILE)
            << "Unsupported image extension '" << ext << "' for '" << name << "'";
        return false;
    }

    bool NormalizeImageData(const ImageData &img, NormalizedImage *out)
    {
        const int w = img.Width(), h = img.Height();
        int ch = img.Channels();
        if (w <= 0 || h <= 0 || ch < 1)
            return false;
        // MIPMap supports 1, 3, or 4 channels. Collapse 2-channel (YA) to gray
        // by dropping alpha.
        if (ch == 2)
            ch = 1;
        else if (ch != 1 && ch != 3 && ch != 4)
        {
            LOG(LUX_ERROR, LUX_BADFILE)
                << "Unsupported channel count " << img.Channels() << " in '"
                << "'";
            return false;
        }

        out->width = w;
        out->height = h;
        out->channels = ch;
        out->data.resize(size_t(w) * h * ch);
        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                for (int c = 0; c < ch; ++c)
                    out->data[(size_t(y) * w + x) * ch + c] = PixelFloat(img, x, y, c);
            }
        }
        return true;
    }

    bool LoadNormalizedImage(const std::string &name, NormalizedImage *out)
    {
        ImageData img;
        if (!ReadImageByExtension(name, &img))
            return false;
        if (!NormalizeImageData(img, out))
        {
            LOG(LUX_ERROR, LUX_BADFILE) << "Failed to normalize image '" << name << "'";
            return false;
        }
        return true;
    }

    std::shared_ptr<MIPMap> GetCachedMIPMap(const TexInfo &info)
    {
        static std::mutex mtx;
        static std::unordered_map<std::string, std::shared_ptr<MIPMap>> cache;

        // Key: filename + filter params.
        const std::string key = info.filename + "|" +
                                std::to_string(int(info.filterType)) + "|" +
                                std::to_string(info.discardmm) + "|" +
                                std::to_string(info.maxAniso) + "|" +
                                std::to_string(int(info.wrapMode));

        std::lock_guard<std::mutex> lock(mtx);
        auto it = cache.find(key);
        if (it != cache.end())
            return it->second;

        NormalizedImage nimg;
        std::shared_ptr<MIPMap> map;
        if (LoadNormalizedImage(info.filename, &nimg))
        {
            map = std::make_shared<MIPMap>(
                info.filterType, nimg.width, nimg.height, nimg.channels,
                nimg.data.data(), info.maxAniso, info.wrapMode);
        }
        else
        {
            // Fallback to a white map.
            LOG(LUX_WARNING, LUX_NOFILE)
                << "Imagemap '" << info.filename
                << "' could not be read; using constant 1";
            const float one = 1.f;
            map = std::make_shared<MIPMap>(info.filterType, 1, 1, 3, &one,
                                           info.maxAniso, info.wrapMode);
        }

        if (info.discardmm > 0 &&
            (info.filterType == ImageFilterType::MipmapTrilinear ||
             info.filterType == ImageFilterType::MipmapEWA))
        {
            map->DiscardMipmaps(info.discardmm);
            LOG(LUX_INFO, LUX_NOERROR)
                << "Discarded " << info.discardmm << " mipmap levels";
        }

        cache[key] = map;
        return map;
    }

} // namespace lux2
