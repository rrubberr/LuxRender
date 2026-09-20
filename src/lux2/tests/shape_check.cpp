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

// Shape tessellation.

#include "core/dynload.h"
#include "core/embree2.h"
#include "core/paramset.h"
#include "core/scene.h"
#include "core/shape.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

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

bool Close(float a, float b, float eps = 1e-3f) {
    return std::fabs(a - b) <= eps * (1.f + std::fabs(a) + std::fabs(b));
}

std::shared_ptr<Shape> Make(const char *plugin, const ParamSet &params,
                            const Transform &xform = Transform()) {
    auto &reg = DynamicLoader::registeredShapes();
    auto it = reg.find(plugin);
    if (it == reg.end())
        return nullptr;
    PluginContext ctx;
    ctx.params = const_cast<ParamSet *>(&params);
    ctx.transform = xform;
    return it->second(ctx);
}

// Sphere: default 64x32 grid -> 2*(63)*(31) = 3906 triangles.
void CheckSphere() {
    std::cout << "[sphere]" << std::endl;
    std::shared_ptr<Shape> s = Make("sphere", ParamSet());
    Check(s != nullptr, "sphere plugin registered");
    if (!s) return;

    std::vector<TriangleDesc> tris;
    s->Tessellate(Transform(), tris);
    Check(tris.size() == 3906, "sphere default grid = 3906 tris");

    // All shading normals unit length.
    bool unit = true;
    for (const auto &t : tris) {
        for (const Normal3f *n : {&t.n0, &t.n1, &t.n2}) {
            const float l2 = n->x() * n->x() + n->y() * n->y() + n->z() * n->z();
            if (!Close(l2, 1.f, 1e-3f)) unit = false;
        }
    }
    Check(unit, "sphere normals unit length");

    // Unit sphere bound ~ [-1,1]^3.
    const BBox b = s->WorldBound();
    Check(b.IsValid() && Close(b.pMin.x(), -1.f) && Close(b.pMax.x(), 1.f),
          "sphere bound ~ unit");

    // Param-driven resolution: 8x4 -> 2*7*3 = 42 tris.
    ParamSet p;
    const int nu = 8, nv = 4;
    p.AddInt("phisegments", &nu, 1);
    p.AddInt("thetasegments", &nv, 1);
    std::shared_ptr<Shape> s2 = Make("sphere", p);
    std::vector<TriangleDesc> tris2;
    s2->Tessellate(Transform(), tris2);
    Check(tris2.size() == 42, "sphere 8x4 grid = 42 tris");
}

// PlyMesh from a real file: floor is 4 verts / 1 quad -> 2 triangles.
void CheckPlyMesh(const std::string &plyPath) {
    std::cout << "[plymesh]" << std::endl;
    ParamSet p;
    p.AddString("filename", &plyPath, 1);
    std::shared_ptr<Shape> s = Make("plymesh", p);
    Check(s != nullptr, "plymesh plugin registered + file read");
    if (!s) return;

    std::vector<TriangleDesc> tris;
    s->Tessellate(Transform(), tris);
    Check(tris.size() == 2, "floor quad split into 2 triangles");

    const BBox b = s->WorldBound();
    Check(b.IsValid(), "plymesh bound valid");
}

// Scene::Commit over {sphere, plymesh}: meshes, triangleCount, Embree build.
void CheckCommit(const std::string &plyPath) {
    std::cout << "[commit]" << std::endl;
    SceneDescription desc;
    {
        ShapeDesc sd;
        sd.name = "sphere";
        sd.toWorld = Transform();
        desc.shapes.push_back(sd);
    }
    {
        ShapeDesc sd;
        sd.name = "plymesh";
        sd.toWorld = Transform();
        sd.params.AddString("filename", &plyPath, 1);
        desc.shapes.push_back(sd);
    }

    Scene scene;
    scene.Commit(desc);
    Check(scene.IsCommitted(), "scene committed");
    Check(desc.meshes.size() == 2, "two meshes produced");
    Check(scene.GetSummary().triangleCount == 3906 + 2,
          "triangleCount = sphere + plymesh");
    Check(scene.GetEmbree() != nullptr, "embree built");
    if (scene.GetEmbree())
        Check(scene.GetEmbree()->TriangleCount() == 3906 + 2,
              "embree triangle count matches");
}

} // namespace

int main(int argc, char **argv) {
    std::string plyPath = argc > 1 ? argv[1] : "";
    std::cout << "lux2 shape_check" << std::endl;

    CheckSphere();
    if (!plyPath.empty()) {
        CheckPlyMesh(plyPath);
        CheckCommit(plyPath);
    } else {
        std::cerr << "  (no .ply path argument; skipping plymesh/commit)"
                  << std::endl;
    }

    if (g_failures == 0) {
        std::cout << "ALL SHAPE CHECKS PASSED" << std::endl;
        return 0;
    }
    std::cerr << g_failures << " shape check(s) FAILED" << std::endl;
    return 1;
}
