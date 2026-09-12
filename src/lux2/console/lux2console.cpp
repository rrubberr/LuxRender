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

// Command line front-end for lux2 renderer.

#include "core/lux2.h"

#include <embree4/rtcore.h>

#include <cstdlib>
#include <iostream>
#include <string>

#ifndef LUX2_VERSION_STRING
#define LUX2_VERSION_STRING "0.1.0"
#endif

namespace {

// Eevaluate a packet operation, then reduce it horizontally.
bool CheckEnoki() {
	// Keep the runtime value small so x*x stays inside float range.
	const lux2::FloatP x = static_cast<float>(std::rand() % 100 + 1);
	const lux2::FloatP y = enoki::fmadd(x, x, lux2::FloatP(1.f));
	const lux2::MaskP m = y > lux2::FloatP(0.f);
	const lux2::FloatP r = enoki::select(m, y, lux2::FloatP(0.f));

	const float sum = enoki::hsum(r);
	std::cout << "enoki: lanes=" << lux2::PACKET_WIDTH
		<< " hsum=" << sum << std::endl;
	return sum > 0.f;
}

// Verify Embree device and scene creation.
bool CheckEmbree() {
	RTCDevice device = rtcNewDevice(nullptr);
	if (!device) {
		std::cerr << "embree: failed to create device" << std::endl;
		return false;
	}

	RTCScene scene = rtcNewScene(device);
	const bool ok = (scene != nullptr);
	if (!ok)
		std::cerr << "embree: failed to create scene" << std::endl;

	if (scene)
		rtcReleaseScene(scene);
	rtcReleaseDevice(device);

	std::cout << "embree: device+scene OK (Embree "
		<< RTC_VERSION_STRING << ")" << std::endl;
	return ok;
}

}// Anonymous namespace.

int main(int argc, char *argv[]) {
	std::cout << "lux2 " << LUX2_VERSION_STRING
		<< " - SoA demo renderer (skeleton)" << std::endl;

	if (!CheckEnoki()) {
		std::cerr << "lux2: Enoki SoA smoke test FAILED" << std::endl;
		return EXIT_FAILURE;
	}
	if (!CheckEmbree()) {
		std::cerr << "lux2: Embree smoke test FAILED" << std::endl;
		return EXIT_FAILURE;
	}

	std::cout << "lux2: all checks passed" << std::endl;
	(void)argc; (void)argv;
	return EXIT_SUCCESS;
}
