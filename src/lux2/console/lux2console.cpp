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

#include "core/api.h"
#include "core/context2.h"
#include "core/lux2.h"
#include "core/plymesh.h"
#include "core/scene.h"

#include <embree4/rtcore.h>

#include <cstdlib>
#include <iostream>
#include <string>

#include <unistd.h>

#ifndef LUX2_VERSION_STRING
#define LUX2_VERSION_STRING "0.1.0"
#endif

namespace
{

	// Evaluate a packet operation, then reduce it horizontally.
	bool CheckEnoki()
	{
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
	bool CheckEmbree()
	{
		RTCDevice device = rtcNewDevice(nullptr);
		if (!device)
		{
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

	// Print the summary for a parsed scene description.
	void PrintSceneSummary(const lux2::SceneDescription &d)
	{
		using namespace lux2;

		std::cout << std::endl
				  << "--- scene summary ---" << std::endl;

		std::cout << "renderer: " << d.rendererName << std::endl;
		std::cout << "sampler: " << d.samplerName << std::endl;
		std::cout << "filter: " << d.filterName << std::endl;
		std::cout << "integrator: " << d.surfIntName
				  << " (volume: " << d.volIntName << ")" << std::endl;

		// Camera and its parameters.
		std::cout << "camera: " << d.cameraName;
		{
			const float fov = d.cameraParams.FindOneFloat("fov", -1.f);
			if (fov >= 0.f)
				std::cout << " fov=" << fov;
			const float so = d.cameraParams.FindOneFloat("shutteropen", -1.f);
			const float sc = d.cameraParams.FindOneFloat("shutterclose", -1.f);
			if (so >= 0.f || sc >= 0.f)
				std::cout << " shutter=[" << so << ", " << sc << "]";
		}
		std::cout << std::endl;

		// Film resolution and filename.
		std::cout << "film: " << d.filmName
				  << " xres=" << d.filmParams.FindOneInt("xresolution", 0)
				  << " yres=" << d.filmParams.FindOneInt("yresolution", 0)
				  << " filename=\"" << d.filmParams.FindOneString("filename", "")
				  << "\"" << std::endl;

		// Named materials.
		std::cout << "named materials: " << d.namedMaterials.size() << std::endl;
		for (const auto &kv : d.namedMaterials)
		{
			std::cout << "  " << kv.first << " ("
					  << kv.second.FindOneString("type", "?") << ")" << std::endl;
		}

		// Textures.
		std::cout << "textures: " << d.textures.size() << std::endl;
		for (const auto &kv : d.textures)
		{
			std::cout << "  " << kv.first << " " << kv.second.textureType
					  << " " << kv.second.pluginName << std::endl;
		}

		// Lights (non-area).
		std::cout << "lights: " << d.lights.size() << std::endl;
		for (const auto &l : d.lights)
			std::cout << "  " << l.name << std::endl;

		// Shapes with material binding, triangle count, and bound.
		std::cout << "shapes: " << d.shapes.size() << std::endl;
		for (const auto &s : d.shapes)
		{
			std::cout << "  " << s.name;

			// Material binding.
			if (s.material.valid())
			{
				std::cout << " material="
						  << (s.material.isNamed ? s.material.namedRef
												 : s.material.pluginName);
			}
			else
			{
				std::cout << " material=<default>";
			}

			if (s.isAreaLight)
				std::cout << " arealight=" << s.areaLightName;

			// Triangle count and bound.
			if (s.name == "plymesh")
			{
				const std::string file = s.params.FindOneString("filename", "");
				if (!file.empty())
				{
					const PlySummary ps = ReadPlySummary(file);
					if (ps.ok)
					{
						std::cout << " tris=" << ps.faceCount;
						if (ps.bound.IsValid())
						{
							std::cout << " bbox=["
									  << ps.bound.pMin.x() << ", "
									  << ps.bound.pMin.y() << ", "
									  << ps.bound.pMin.z() << "] - ["
									  << ps.bound.pMax.x() << ", "
									  << ps.bound.pMax.y() << ", "
									  << ps.bound.pMax.z() << "]";
						}
					}
					else
					{
						std::cout << " tris=? (ply read failed)";
					}
				}
			}
			else if (s.name == "sphere")
			{
				std::cout << " tris=0 (analytic)";
			}

			std::cout << std::endl;
		}

		std::cout << "--- end summary ---" << std::endl;
	}

	int RunParse(const char *file)
	{
		std::cout << "lux2: parsing " << file << std::endl;

		// chdir to the scene directory then restore the original afterwards.
		std::string origDir;
		{
			char buf[4096];
			if (getcwd(buf, sizeof(buf)))
				origDir = buf;
		}
		const std::string scenePath(file);
		const std::size_t slash = scenePath.find_last_of('/');
		if (slash != std::string::npos && slash > 0)
			chdir(scenePath.substr(0, slash).c_str());

		luxInit();
		const int ok = luxParse(scenePath.substr(slash == std::string::npos ? 0 : slash + 1).c_str());
		if (!ok)
		{
			std::cerr << "lux2: parse failed" << std::endl;
			luxCleanup();
			if (!origDir.empty())
				chdir(origDir.c_str());
			return EXIT_FAILURE;
		}

		// The scene was committed at WorldEnd; report the recorded description.
		if (lux2::Context2::GetActive())
		{
			PrintSceneSummary(lux2::Context2::GetActive()->Description());

			// Tessellate every shape and construct the Embree
			// accelerator, then report triangle total.
			lux2::Scene scene;
			scene.Commit(lux2::Context2::GetActive()->Description());
			std::cout << "scene: triangles=" << scene.GetSummary().triangleCount
					  << " meshes=" << scene.GetSummary().shapeCount << std::endl;
		}

		luxCleanup();
		if (!origDir.empty())
			chdir(origDir.c_str());
		std::cout << "lux2: parse OK" << std::endl;
		return EXIT_SUCCESS;
	}

} // anonymous namespace

int main(int argc, char *argv[])
{
	std::cout << "lux2 " << LUX2_VERSION_STRING
			  << " - SoA renderer (B.16 parser)" << std::endl;

	if (!CheckEnoki())
	{
		std::cerr << "lux2: Enoki SoA smoke test FAILED" << std::endl;
		return EXIT_FAILURE;
	}
	if (!CheckEmbree())
	{
		std::cerr << "lux2: Embree smoke test FAILED" << std::endl;
		return EXIT_FAILURE;
	}

	if (argc >= 2)
		return RunParse(argv[1]);

	std::cout << "lux2: all checks passed" << std::endl;
	return EXIT_SUCCESS;
}
