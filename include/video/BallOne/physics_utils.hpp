#pragma once
#include <box2d/box2d.h>
#include <vector>
#include <cmath>

namespace BallOne::Physics {
    inline std::vector<b2Vec2> get_poly_vertices(float radius, int sides) {
        std::vector<b2Vec2> verts;
        for (int i = 0; i < sides; ++i) {
            float angle = (i / (float)sides) * 2.0f * M_PI;
            verts.push_back({radius * cosf(angle), radius * sinf(angle)});
        }
        return verts;
    }

    inline void create_thick_container(b2WorldId worldId, b2BodyId bodyId, float radius, int sides) {
        // 限制边数上限 120
        int s = std::min(sides, 120);
        auto inner_verts = get_poly_vertices(radius, s);
        float thickness = 0.5f;

        b2ShapeDef sd = b2DefaultShapeDef();
        sd.material.restitution = 1.0f;
        sd.material.friction = 0.0f;
        sd.enableHitEvents = true;

        for (int i = 0; i < s; ++i) {
            int j = (i + 1) % s;
            b2Vec2 v[4];
            v[0] = inner_verts[i];
            v[1] = inner_verts[j];

            float angle_i = (i / (float)s) * 2.0f * M_PI;
            float angle_j = (j / (float)s) * 2.0f * M_PI;
            v[2] = { (radius + thickness) * cosf(angle_j), (radius + thickness) * sinf(angle_j) };
            v[3] = { (radius + thickness) * cosf(angle_i), (radius + thickness) * sinf(angle_i) };

            b2Hull hull = b2ComputeHull(v, 4);
            if (hull.count < 3) continue;
            b2Polygon poly = b2MakePolygon(&hull, 0.0f);
            b2CreatePolygonShape(bodyId, &sd, &poly);
        }
    }
}