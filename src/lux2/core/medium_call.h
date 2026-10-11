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

#ifndef LUX2_MEDIUM_CALL_H
#define LUX2_MEDIUM_CALL_H

#include "core/medium.h"

#include <enoki/array.h>

// Medium dispatch.
ENOKI_CALL_SUPPORT_BEGIN(lux2::Medium)
ENOKI_CALL_SUPPORT_METHOD(Flags)
ENOKI_CALL_SUPPORT_METHOD(SigmaA)
ENOKI_CALL_SUPPORT_METHOD(SigmaS)
ENOKI_CALL_SUPPORT_METHOD(Lve)
ENOKI_CALL_SUPPORT_METHOD(SampleInteraction)
ENOKI_CALL_SUPPORT_METHOD(Transmittance)
ENOKI_CALL_SUPPORT_METHOD(Tau)
ENOKI_CALL_SUPPORT_METHOD(PhaseSample)
ENOKI_CALL_SUPPORT_METHOD(PhasePdf)
ENOKI_CALL_SUPPORT_METHOD(IntersectAABB)
ENOKI_CALL_SUPPORT_END(lux2::Medium)

#endif // LUX2_MEDIUM_CALL_H
