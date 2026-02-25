#include <box2d/box2d.h>

namespace Physics {

    inline void create_thick_ring_layer(b2WorldId worldId, b2BodyId bodyId, float radius, float thickness, float hole_half_angle, uint32_t ringIdx, const b2SurfaceMaterial* mat) {
        const int total_segments = 72;
        float segment_step = (2.0f * M_PI) / total_segments;

        b2ShapeDef shapeDef = b2DefaultShapeDef();
        shapeDef.material = *mat;
        shapeDef.filter.categoryBits = CATEGORY_RING;
        shapeDef.filter.maskBits = CATEGORY_BALL;
        shapeDef.userData = (void*)(intptr_t)(ringIdx + 100);

        for (int i = 0; i < total_segments; ++i) {
            float angle = i * segment_step;
            float norm_angle = fmodf(angle, 2.0f * M_PI);

            if (norm_angle < hole_half_angle || norm_angle > (2.0f * M_PI - hole_half_angle)) {
                continue;
            }

            float r_in = radius - thickness * 0.5f;
            float r_out = radius + thickness * 0.5f;
            float next_angle = angle + segment_step;

            // 1. 定义顶点
            b2Vec2 vertices[4];
            vertices[0] = {r_in * cosf(angle), r_in * sinf(angle)};
            vertices[1] = {r_out * cosf(angle), r_out * sinf(angle)};
            vertices[2] = {r_out * cosf(next_angle), r_out * sinf(next_angle)};
            vertices[3] = {r_in * cosf(next_angle), r_in * sinf(next_angle)};

            // 2. [关键修复]: 先计算凸包 (Hull)
            b2Hull hull = b2ComputeHull(vertices, 4);

            // 3. [关键修复]: 使用 Hull 创建 Polygon
            // 参数为: (Hull指针, 圆角半径)
            b2Polygon poly = b2MakePolygon(&hull, 0.0f);

            b2CreatePolygonShape(bodyId, &shapeDef, &poly);
        }
    }
}