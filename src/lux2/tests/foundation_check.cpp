/***************************************************************************
 *   This file is part of LuxRender.                                       *
 *                                                                         *
 *   To the extent possible under law, the author(s) have dedicated all    *
 *   copyright and related neighboring rights to the belowe code to the    *
 *   public domain worldwide. The below code is distributed without any    *
 *   warranty.                                                             *
 *                                                                         *
 *   See: <https://creativecommons.org/publicdomain/zero/1.0/>             *
 *                                                                         *
 ***************************************************************************/
// Tests are machine generated. Proceed with caution!

// Ensure Context2 records a SceneDescription and Scene::Commit validates.

#include "core/api.h"
#include "core/context2.h"
#include "core/scene.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <thread>

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
// plyPath points at a real PLY so Commit can tessellate it without erroring.
void CheckRecorder(const std::string &plyPath) {
	Context2 ctx;
	Context2::SetActive(&ctx);
	Check(Context2::GetActive() == &ctx, "active context get/set");

	// Options block. Only "lowdiscrepancy" and "gaussian" are registered
	// in-tree right now; Commit logs and skips unknown names.
	ctx.Renderer("sampler", ParamSet());
	ctx.Sampler("lowdiscrepancy", ParamSet());
	ctx.SurfaceIntegrator("path", FloatParam("maxdepth", 8.f));
	ctx.VolumeIntegrator("none", ParamSet());
	ctx.PixelFilter("gaussian", FloatParam("xwidth", 3.3f));
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
	ctx.Shape("plymesh", StringParam("filename", plyPath));
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

	// A non-area light source. "infinite" is out of the tree; Commit must
	// skip the unregistered plugin without derailing the rest.
	{
		const float power = 50.f;
		ParamSet env;
		env.AddFloat("power", &power, 1);
		ctx.LightSource("infinite", env);
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

	Check(d.lights[0].name == "infinite", "light plugin name recorded");

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
	// WorldEnd auto-starts rendering (startRenderingAfterParse defaults to
	// true). This test only exercises statement rejection, so opt out:
	// without this the destructor joins a real render thread (default film
	// haltSpp=-1, unbounded) and ~ctx's Terminate races Render()'s initial
	// m_state.store(Run), losing the cancellation and hanging forever.
	ctx.StartRenderingAfterParse(false);

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

// Drive the C ABI (lux* / lux*V) end-to-end and verify the recorded
// description, the transform math, and that every deferred stub is safe to
// call.
void CheckAPI() {
	Check(luxVersion() != nullptr, "luxVersion returns a string");

	luxInit();
	Check(Context2::GetActive() != nullptr, "luxInit installs an active context");
	Check(Context2::GetActive()->State() == LUX2_STATE_OPTIONS_BLOCK,
		"luxInit enters the options block");

	// --- Options block via the V dispatchers -----------------------------
	luxRendererV("sampler", 0, nullptr, nullptr);
	luxSamplerV("lowdiscrepancy", 0, nullptr, nullptr);
	luxPixelFilterV("gaussian", 0, nullptr, nullptr);
	luxSurfaceIntegratorV("path", 0, nullptr, nullptr);
	luxVolumeIntegratorV("none", 0, nullptr, nullptr);

	{
		// haltspp bounds the render that luxStart() launches below; without
		// it the pass loop only ends when luxExit()/luxWait() land.
		const int xres = 320, yres = 240, haltspp = 16;
		const LuxToken toks[] = {"integer xresolution", "integer yresolution",
		                         "integer haltspp"};
		const LuxPointer ps[] = {(const char *)&xres, (const char *)&yres,
		                         (const char *)&haltspp};
		luxFilmV("fleximage", 3, toks, ps);
	}
	{
		const float fov = 45.f;
		const LuxToken toks[] = {"float fov"};
		const LuxPointer ps[] = {(const char *)&fov};
		luxCameraV("perspective", 1, toks, ps);
	}

	// --- World block -----------------------------------------------------
	luxWorldBegin();

	{
		const float kd[3] = {0.7f, 0.7f, 0.7f};
		const char *type = "matte";
		const LuxToken toks[] = {"color Kd", "string type"};
		const LuxPointer ps[] = {(const char *)kd, (const char *)type};
		luxMakeNamedMaterialV("wall_white", 2, toks, ps);
	}

	// A shape under a translate, bound to the named material.
	luxAttributeBegin();
	luxTranslate(2.f, 0.f, 0.f);
	luxNamedMaterial("wall_white");
	{
		const float radius = 0.5f;
		const LuxToken toks[] = {"float radius"};
		const LuxPointer ps[] = {(const char *)&radius};
		luxShapeV("sphere", 1, toks, ps);
	}
	luxAttributeEnd();

	// An area light bound to a sphere.
	luxAttributeBegin();
	{
		const float L[3] = {1.f, 1.f, 1.f};
		const LuxToken toks[] = {"color L"};
		const LuxPointer ps[] = {(const char *)L};
		luxAreaLightSourceV("area", 1, toks, ps);
	}
	{
		const float radius = 0.1f;
		const LuxToken toks[] = {"float radius"};
		const LuxPointer ps[] = {(const char *)&radius};
		luxShapeV("sphere", 1, toks, ps);
	}
	luxAttributeEnd();

	// A fresnel texture.
	{
		const float eta = 1.5f;
		const LuxToken toks[] = {"float eta"};
		const LuxPointer ps[] = {(const char *)&eta};
		luxTextureV("metal_nk", "fresnel", "fresnelcolor", 1, toks, ps);
	}

	luxWorldEnd();

	// WorldEnd commits the description and — because startRenderingAfterParse
	// defaults to true — spawns the render thread right here, before any
	// luxStart() call. It is bounded by haltspp=16 above.
	Context2 *ctx = Context2::GetActive();

	// --- Verify the recorded description ---------------------------------
	SceneDescription &d = Context2::GetActive()->Description();
	Check(d.rendererName == "sampler", "C API: renderer recorded");
	Check(d.samplerName == "lowdiscrepancy", "C API: sampler recorded");
	Check(d.filterName == "gaussian", "C API: filter recorded");
	Check(d.surfIntName == "path", "C API: surface integrator recorded");
	Check(d.cameraName == "perspective", "C API: camera recorded");
	Check(d.filmName == "fleximage", "C API: film recorded");
	Check(d.filmParams.FindOneInt("xresolution", -1) == 320,
		"C API: film xresolution guessed from integer token");
	Check(d.filmParams.FindOneInt("yresolution", -1) == 240,
		"C API: film yresolution guessed from integer token");
	Check(d.filmParams.FindOneInt("haltspp", -999) == 16,
		"C API: film haltspp guessed from integer token");
	Check(std::fabs(d.cameraParams.FindOneFloat("fov", 0.f) - 45.f) < 1e-5f,
		"C API: camera fov guessed from float token");

	Check(d.shapes.size() == 2, "C API: two shapes recorded");
	Check(d.namedMaterials.count("wall_white") == 1,
		"C API: named material recorded");
	Check(d.textures.count("metal_nk") == 1, "C API: texture recorded");
	Check(d.textures.count("metal_nk") == 1 &&
		d.textures["metal_nk"].textureType == "fresnel",
		"C API: texture type recorded");

	if (d.shapes.size() == 2) {
		const ShapeDesc &wall = d.shapes[0];
		Check(wall.name == "sphere", "C API: shape 0 plugin name");
		Check(wall.material.isNamed && wall.material.namedRef == "wall_white",
			"C API: shape 0 named material binding");
		// Translate(2,0,0) => translation column x == 2.
		Check(std::fabs(wall.toWorld.translation().x() - 2.f) < 1e-4f,
			"C API: shape 0 transform carries translate");

		const ShapeDesc &lamp = d.shapes[1];
		Check(lamp.isAreaLight && lamp.areaLightName == "area",
			"C API: shape 1 is an area light");
	}

	// Commit validates and tallies the API-built description.
	Scene scene;
	scene.Commit(d);
	Check(scene.IsCommitted(), "C API: scene committed");
	Check(scene.GetSummary().shapeCount == 2, "C API: summary shape count");
	Check(scene.GetSummary().areaLightShapeCount == 1,
		"C API: summary area-light count");
	Check(scene.GetSummary().namedMaterialCount == 1,
		"C API: summary named-material count");

	// --- Render control ----------------------------------------------------
	// The render thread was spawned by luxWorldEnd above and is bounded by
	// haltspp=16. Pause before Exit: the outer pause/resume loop only exits
	// on Terminate, so a Pause that outlives Exit would block it forever.
	luxStart();
	luxPause();
	luxExit();
	luxAbort();
	{
		// luxWait() blocks; run it on a helper thread so a lost cancellation
		// fails loudly instead of stalling ctest. Exit/Abort above already
		// set Terminate, so the render loop breaks after its current item.
		std::atomic<bool> waitDone{false};
		std::thread waiter([&] { luxWait(); waitDone.store(true); });
		const long budgetMs = 30000;
		long waitedMs = 0;
		while (!waitDone.load() && waitedMs < budgetMs) {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			waitedMs += 100;
		}
		if (!waitDone.load()) {
			std::cerr << "  FAIL: luxWait did not return within " << budgetMs
			          << "ms — render is unbounded" << std::endl;
			if (Scene *s = ctx->GetScene(); s && s->IsCommitted())
				std::cerr << "  diag: spp=" << s->GetFilm().SampleCount()
				          << " haltSpp=" << s->GetFilm().HaltSpp()
				          << " haltTime=" << s->GetFilm().HaltTime() << std::endl;
			std::cout.flush();
			// The render thread ignores Terminate; exiting the process is
			// the only escape. Status 2 marks a hang, not a check failure.
			std::_Exit(2);
		}
		waiter.join();
	}
	Check(true, "C API: luxWait returns after Exit/Abort (render bounded)");
	luxSetThreadCount(4);
	Check(luxGetThreadCount() == 4, "C API: luxGetThreadCount reflects set");
	// The film is a real fleximage after a completed render, so the
	// framebuffer accessors hand out allocated buffers.
	luxUpdateFramebuffer();
	Check(luxFramebuffer() != nullptr, "C API: luxFramebuffer non-null");
	Check(luxFloatFramebuffer() != nullptr,
		"C API: luxFloatFramebuffer non-null");
	Check(luxAlphaBuffer() != nullptr, "C API: luxAlphaBuffer non-null");
	luxSetUserSamplingMap(nullptr);
	Check(luxGetUserSamplingMap() == nullptr,
		"C API: luxGetUserSamplingMap null");
	luxGetHistogramImage(nullptr, 0, 0, 0);
	Check(luxSaveEXR("out.exr", false, false, 0, false) == 0,
		"C API: luxSaveEXR stub returns 0");
	luxSetParameterValue(LUX_FILM, LUX_FILM_TM_LINEAR_GAMMA, 2.2);
	Check(luxGetParameterValue(LUX_FILM, LUX_FILM_TM_LINEAR_GAMMA) == 0.0,
		"C API: luxGetParameterValue stub returns 0");
	Check(luxGetDefaultParameterValue(LUX_FILM, LUX_FILM_TM_LINEAR_GAMMA) == 0.0,
		"C API: luxGetDefaultParameterValue stub returns 0");
	{
		char buf[8];
		Check(luxGetStringParameterValue(LUX_FILM, LUX_FILM_TM_TONEMAPKERNEL,
			buf, sizeof(buf)) == 0,
			"C API: luxGetStringParameterValue stub returns 0");
	}

	luxCleanup();
	Check(Context2::GetActive()->State() == LUX2_STATE_UNINITIALIZED,
		"luxCleanup returns to uninitialized");
}

// Verify Scene::Commit instantiates materials/textures and builds the
// BsdfPtrTable.
void CheckMaterials(const std::string &plyPath) {
	Context2 ctx;
	Context2::SetActive(&ctx);

	ctx.Renderer("sampler", ParamSet());
	ctx.Sampler("lowdiscrepancy", ParamSet());
	ctx.SurfaceIntegrator("path", ParamSet());
	ctx.VolumeIntegrator("none", ParamSet());
	ctx.PixelFilter("gaussian", ParamSet());
	ctx.Camera("perspective", ParamSet());
	ctx.Film("fleximage", ParamSet());

	ctx.WorldBegin();

	// A fresnelcolor texture (the one plugin metal2 will consume in Phase 4).
	{
		const RGBColor kr(0.7f, 0.7f, 0.7f);
		ParamSet fp;
		fp.AddRGBColor("Kr", &kr, 1);
		ctx.Texture("metal_cube_nk", "fresnel", "fresnelcolor", fp);
	}

	// Named materials mirroring the Cornell set.
	{
		const RGBColor kd(0.7f, 0.7f, 0.7f);
		const std::string type = "matte";
		ParamSet m;
		m.AddRGBColor("Kd", &kd, 1);
		m.AddString("type", &type, 1);
		ctx.MakeNamedMaterial("wall_white", m);
	}
	{
		const float ur = 0.01f, vr = 0.01f;
		const std::string type = "metal2";
		ParamSet m;
		m.AddFloat("uroughness", &ur, 1);
		m.AddFloat("vroughness", &vr, 1);
		m.AddTexture("fresnel", "metal_cube_nk");
		m.AddString("type", &type, 1);
		ctx.MakeNamedMaterial("metal_cube", m);
	}
	{
		const float index = 1.5f;
		const RGBColor kr(0.8f, 0.8f, 0.8f), kt(0.8f, 0.8f, 0.8f);
		const std::string type = "glass";
		ParamSet m;
		m.AddFloat("index", &index, 1);
		m.AddRGBColor("Kr", &kr, 1);
		m.AddRGBColor("Kt", &kt, 1);
		m.AddString("type", &type, 1);
		ctx.MakeNamedMaterial("glass_ball", m);
	}
	{
		const std::string type = "null";
		ParamSet m;
		m.AddString("type", &type, 1);
		ctx.MakeNamedMaterial("Lamp", m);
	}

	// Shapes: one per named material, plus one unbound (default material).
	ctx.AttributeBegin();
	ctx.NamedMaterial("wall_white");
	ctx.Shape("plymesh", StringParam("filename", plyPath));
	ctx.AttributeEnd();

	ctx.AttributeBegin();
	ctx.NamedMaterial("metal_cube");
	ctx.Shape("sphere", FloatParam("radius", 0.5f));
	ctx.AttributeEnd();

	ctx.AttributeBegin();
	ctx.NamedMaterial("glass_ball");
	ctx.Shape("sphere", FloatParam("radius", 0.3f));
	ctx.AttributeEnd();

	ctx.AttributeBegin();  // unbound -> default material (id 0)
	ctx.Shape("sphere", FloatParam("radius", 0.2f));
	ctx.AttributeEnd();

	SceneDescription &d = ctx.Description();
	ctx.WorldEnd();

	Scene scene;
	scene.Commit(d);

	// 4 named materials + 1 default (id 0) = 5.
	Check(d.materials.size() == 5, "materials: 4 named + default = 5");
	Check(scene.GetSummary().namedMaterialCount == 4,
		"materials: named-material count == 4");

	bool allMat = true;
	for (const auto &m : d.materials)
		if (!m) allMat = false;
	Check(allMat, "materials: every entry non-null");

	const BsdfPtrTable &table = scene.GetBsdfTable();
	Check(table.ptrs.size() == d.materials.size(),
		"materials: BsdfPtrTable size == material count");
	bool allPtr = true;
	for (const auto *b : table.ptrs)
		if (!b) allPtr = false;
	Check(allPtr, "materials: every BSDF pointer non-null");

	// Default material (id 0) is the null material.
	Check((table.ptrs[0]->flags() & uint32_t(BSDFType::Null)) != 0u,
		"materials: default (id 0) is a Null BSDF");

	// Every triangle's matID is in range; the unbound shape maps to id 0.
	bool inRange = true;
	bool unboundIsZero = false;
	for (const auto &md : d.meshes) {
		for (const auto &td : md.tris) {
			if (td.matID >= table.ptrs.size()) inRange = false;
		}
	}
	// The 4th mesh (index 3) is the unbound sphere.
	if (d.meshes.size() == 4 && !d.meshes[3].tris.empty())
		unboundIsZero = d.meshes[3].tris[0].matID == 0;
	Check(inRange, "materials: every triangle matID < table size");
	Check(unboundIsZero, "materials: unbound shape resolves to default id 0");

	// The fresnelcolor texture is recorded and typed for later resolution.
	Check(d.textures.count("metal_cube_nk") == 1 &&
		d.textures["metal_cube_nk"].textureType == "fresnel",
		"materials: fresnelcolor texture recorded as fresnel");
}

} // anonymous namespace

int main(int argc, char **argv) {
	std::cout << "lux2 B.15 foundation check" << std::endl;

	const std::string plyPath = argc > 1 ? argv[1] : "wall.ply";
	CheckRecorder(plyPath);
	CheckStateMachine();
	CheckAPI();
	CheckMaterials(plyPath);

	if (g_failures == 0) {
		std::cout << "lux2foundationcheck: ALL CHECKS PASSED" << std::endl;
		return EXIT_SUCCESS;
	}
	std::cerr << "lux2foundationcheck: " << g_failures << " CHECK(S) FAILED"
		<< std::endl;
	return EXIT_FAILURE;
}
