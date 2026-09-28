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

#ifndef LUX2_PNGIO_H
#define LUX2_PNGIO_H

#include "core/imagereader.h"
#include "core/spectrum.h"

#include <string>
#include <vector>

namespace lux2
{

    class ColorSystem;

    bool WritePngImage(int channeltype, bool bit16, const std::string &name,
                       const std::vector<RGBColor> &pixels,
                       const std::vector<float> &alpha,
                       int xPixelCount, int yPixelCount,
                       int xResolution, int yResolution,
                       int xPixelStart, int yPixelStart,
                       const ColorSystem &cSystem, float screenGamma);

    bool ReadPngImage(const std::string &name, ImageData *out,
                      float *gamma = nullptr);

} // namespace lux2

#endif // LUX2_PNGIO_H
