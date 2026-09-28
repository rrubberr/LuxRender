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

#ifndef LUX2_TIFFIO_H
#define LUX2_TIFFIO_H

#include "core/imagereader.h"
#include "core/spectrum.h"

#include <string>
#include <vector>

namespace lux2
{

    class ColorSystem;

    // TIFF compression codes.
    enum TiffCompression
    {
        TIFF_COMPRESSION_NONE = 0,
        TIFF_COMPRESSION_LZW = 1,
        TIFF_COMPRESSION_DEFLATE = 2
    };

    // bitdepth selects the sample format.
    bool WriteTiffImage(int channeltype, int bitdepth, int compressiontype,
                        const std::string &name,
                        const std::vector<RGBColor> &pixels,
                        const std::vector<float> &alpha,
                        int xPixelCount, int yPixelCount,
                        int xResolution, int yResolution,
                        int xPixelStart, int yPixelStart,
                        const ColorSystem &cSystem, float screenGamma);

    // Returns native sample type.
    bool ReadTiffImage(const std::string &name, ImageData *out);

} // namespace lux2

#endif // LUX2_TIFFIO_H
