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

// Ensure Context2 records a SceneDescription and Scene::Commit validates.

#include "core/context2.h"
#include "core/scene.h"

#include <cstdlib>
#include <iostream>

using namespace lux2;

namespace {

int g_failures = 0;

void Check(bool cond, const char *what) {
	if (!cond) {
		std::cerr << "  FAIL: " << what << std::endl;
		++g_failures;
	} else {
		std::cout << "  ok:   " << what << std::endl;
	}
}

ParamSet FloatParam(const std::string &name, float v) {
	ParamSet p;
	p.AddFloat(name, &v, 1);
	return p;
}

ParamSet StringParam(const std::string &name, const std::string &v) {
	ParamSet p;
	p.AddString(name, &v, 1);
	return p;
}

// Build a small Cornell-like description via Context2 and verify the record.
void CheckRecorder() {
	Context2 ctx;
	Context2::SetActive(&ctx);
	Check(Context2::GetActive() == &ctx, "active context get/set");

	// Options block.
	ctx.Renderer("sampler", ParamSet());
	ctx.Sampler("random", ParamSet());
	ctx.SurfaceIntegrator("path", FloatParam("maxdepth", 8.f));
	ctx.VolumeIntegrator("none", ParamSet());
	ctx.PixelFilter("blackmanharris", FloatParam("xwidth", 3.3f));
	ctx.Camera("perspective", FloatParam("fov", 39.6f));
	ctx.Film("fleximage", FloatParam("gamma", 2.2f));

	// LookAt before Camera is captured into cameraTransform only when Camera
	// is called; here we set it after to confirm the transform stack records.
	ctx.LookAt(0.f, -4.f, 1.f, 0.f, -3.f, 1.f, 0.f, 0.f, 1.f);

	const SceneDescription &d = ctx.Description();
	Check(d.rendererName == "sampler", "renderer recorded");
	Check(d.surfIntName == "path", "surface integrator recorded");
	Check(d.cameraName == "perspective", "camera recorded");
	Check(d.filmName == "fleximage", "film recorded");

	// World block.
	ctx.WorldBegin();

	// A named material.
	{
		const RGBColor kd(0.7f, 0.7f, 0.7f);
		const std::string type = "matte";
		ParamSet m;
		m.AddRGBColor("Kd", &kd, 1);
		m.AddString("type", &type, 1);
		ctx.MakeNamedMaterial("wall_white", m);
	}

	// A shape bound to the named material, under a translate.
	ctx.AttributeBegin();
	ctx.Translate(1.f, 0.f, 0.f);
	ctx.NamedMaterial("wall_white");
	ctx.Shape("plymesh", StringParam("filename", "wall.ply"));
	ctx.AttributeEnd();

	// An area light bound to a sphere.
	ctx.AttributeBegin();
	ctx.LightGroup("default", ParamSet());
	ctx.NamedMaterial("wall_white");
	{
		const RGBColor L(1.f, 1.f, 1.f);
		ParamSet al;
		al.AddRGBColor("L", &L, 1);
		ctx.AreaLightSource("area", al);
	}
	ctx.Shape("sphere", FloatParam("radius", 0.1f));
	ctx.AttributeEnd();

	// A non-area light source.
	{
		const float power = 50.f;
		ParamSet sun;
		sun.AddFloat("power", &power, 1);
		ctx.LightSource("sun", sun);
	}

	ctx.WorldEnd();

	// Verify the recorded description.
	Check(d.shapes.size() == 2, "two shapes recorded");
	Check(d.lights.size() == 1, "one light recorded");
	Check(d.namedMaterials.count("wall_white") == 1, "named material recorded");

	const ShapeDesc &wall = d.shapes[0];
	Check(wall.name == "plymesh", "shape 0 plugin name");
	Check(wall.material.isNamed && wall.material.namedRef == "wall_white",
		"shape 0 named material binding");
	// Translate(1,0,0) => translation column is (1,0,0).
	const Vector3f t = wall.toWorld.translation();
	Check(std::fabs(t.x() - 1.f) < 1e-4f && std::fabs(t.y()) < 1e-4f,
		"shape 0 transform carries translate");

	const ShapeDesc &lamp = d.shapes[1];
	Check(lamp.isAreaLight && lamp.areaLightName == "area",
		"shape 1 is an area light");
	Check(lamp.lightGroup == "default", "area light group recorded");

	Check(d.lights[0].name == "sun", "light plugin name recorded");

	// Commit validates and tallies.
	Scene scene;
	scene.Commit(const_cast<SceneDescription &>(d));
	Check(scene.IsCommitted(), "scene committed");
	Check(scene.GetSummary().shapeCount == 2, "summary shape count");
	Check(scene.GetSummary().areaLightShapeCount == 1, "summary area-light count");
	Check(scene.GetSummary().lightCount == 1, "summary light count");
	Check(scene.GetSummary().namedMaterialCount == 1, "summary named-material count");
}

// Verify the state machine rejects out-of-order statements.
void CheckStateMachine() {
	Context2 ctx;

	// Shape before WorldBegin must be rejected (state stays options).
	ctx.Shape("sphere", ParamSet());
	Check(ctx.Description().shapes.empty(), "shape rejected before WorldBegin");

	// Renderer after WorldBegin must be rejected.
	ctx.WorldBegin();
	ctx.Renderer("sampler", ParamSet());
	Check(ctx.Description().rendererName.empty(),
		"renderer rejected after WorldBegin");
	ctx.WorldEnd();
}

} // anonymous namespace

int main() {
	std::cout << "lux2 B.15 foundation check" << std::endl;

	CheckRecorder();
	CheckStateMachine();

	if (g_failures == 0) {
		std::cout << "lux2foundationcheck: ALL CHECKS PASSED" << std::endl;
		return EXIT_SUCCESS;
	}
	std::cerr << "lux2foundationcheck: " << g_failures << " CHECK(S) FAILED"
		<< std::endl;
	return EXIT_FAILURE;
}
