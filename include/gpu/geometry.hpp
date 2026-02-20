//
// Created by friendships666 on 2/19/26.
//

#ifndef GEOENGINE_GEOMETRY_HPP
#define GEOENGINE_GEOMETRY_HPP
#pragma once
#include <vector>
#include <cmath>
#include <inttypes.h>
namespace gpu {

    struct Vertex {
        float pos[3];
        float normal[3];
    };

    inline void generateSphere(int latLines, int lonLines, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) {
        for (int lat = 0; lat <= latLines; ++lat) {
            float theta = lat * (float)M_PI / latLines;
            for (int lon = 0; lon <= lonLines; ++lon) {
                float phi = lon * 2.0f * (float)M_PI / lonLines;
                float x = std::cos(phi) * std::sin(theta);
                float y = std::cos(theta);
                float z = std::sin(phi) * std::sin(theta);
                vertices.push_back({{x, y, z}, {x, y, z}});
            }
        }
        for (int lat = 0; lat < latLines; ++lat) {
            for (int lon = 0; lon < lonLines; ++lon) {
                uint32_t first = (lat * (lonLines + 1)) + lon;
                uint32_t second = first + lonLines + 1;
                indices.push_back(first); indices.push_back(second); indices.push_back(first + 1);
                indices.push_back(second); indices.push_back(second + 1); indices.push_back(first + 1);
            }
        }
    }

} // namespace gpu
#endif //GEOENGINE_GEOMETRY_HPP