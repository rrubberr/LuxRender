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

#ifndef LUX2_LIGHT_CALL_H
#define LUX2_LIGHT_CALL_H

#include "core/light.h"
#include "core/vecp.h"

#include <enoki/array.h>

namespace lux2
{
    // Light pointer array.
    using LightPtr = enoki::replace_scalar_t<FloatP, const Light *>;
} // namespace lux2

// IsInfinite() returns a scalar bool and is not dispatched.y.
ENOKI_CALL_SUPPORT_BEGIN(lux2::Light)
ENOKI_CALL_SUPPORT_METHOD(flags)
ENOKI_CALL_SUPPORT_METHOD(Le)
ENOKI_CALL_SUPPORT_METHOD(Sample_L)
ENOKI_CALL_SUPPORT_METHOD(Pdf_L)
ENOKI_CALL_SUPPORT_END(lux2::Light)

#endif // LUX2_LIGHT_CALL_H
