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

#ifndef LUX2_READIMAGE_H
#define LUX2_READIMAGE_H

#include "core/imageio/imagereader.h"
#include "core/mipmap.h"

#include <memory>
#include <string>
#include <vector>

namespace lux2
{

    // Load an image by extension (.exr/.png/.jpg/.jpeg/.tif/.tiff) into a
    // typed ImageData.
    bool ReadImageByExtension(const std::string &name, ImageData *out);

    // Normalized interleaved float pixels produced for a MIPMap.
    struct NormalizedImage
    {
        int width = 0;
        int height = 0;
        int channels = 0; // 1, 3, or 4
        std::vector<float> data;
    };

    // Convert any ImageData (char/short/float, 1-4 channels) to normalized
    // interleaved float.
    bool NormalizeImageData(const ImageData &img, NormalizedImage *out);

    // Read + normalize in one step.
    bool LoadNormalizedImage(const std::string &name, NormalizedImage *out);

    // Parameters that identify a cached MIPMap.
    struct TexInfo
    {
        ImageFilterType filterType;
        std::string filename;
        int discardmm;
        float maxAniso;
        ImageWrap wrapMode;

        bool operator<(const TexInfo &o) const
        {
            if (filterType != o.filterType)
                return filterType < o.filterType;
            if (filename != o.filename)
                return filename < o.filename;
            if (discardmm != o.discardmm)
                return discardmm < o.discardmm;
            if (maxAniso != o.maxAniso)
                return maxAniso < o.maxAniso;
            return wrapMode < o.wrapMode;
        }
    };

    // MIPMap cache keyed by TexInfo.
    std::shared_ptr<MIPMap> GetCachedMIPMap(const TexInfo &info);

} // namespace lux2

#endif // LUX2_READIMAGE_H
