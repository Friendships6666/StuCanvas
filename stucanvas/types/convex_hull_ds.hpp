// include/stucanvas/types/convex_hull_ds.hpp
#pragma once

#include <vector>
#include <list>
#include <cstdint>
#include <limits>
#include <algorithm>
#include "point.hpp"

namespace StuCanvas {

template <typename T>
class ConvexHullDS3D {
public:
    using Index = uint32_t;
    static constexpr Index INVALID_IDX = std::numeric_limits<Index>::max();

    struct Vertex {
        Index face;
        Index point_idx;
    };

    struct Face {
        Index vertices[3]{};
        Index neighbors[3]{};
        std::list<Index> outside_points;
        int info{};
        void* list_iterator{};
    };

    std::vector<Vertex> vertices;
    std::vector<Face>   faces;
    const std::vector<Point3D<T>>* points_ptr;

    ConvexHullDS3D() : points_ptr(nullptr) {}

    void bind_points(const std::vector<Point3D<T>>& pts) { points_ptr = &pts; }

    Index add_vertex(Index point_idx) {
        vertices.push_back({INVALID_IDX, point_idx});
        return static_cast<Index>(vertices.size() - 1);
    }

    Index add_face(Index v0, Index v1, Index v2) {
        faces.push_back({{v0, v1, v2}, {INVALID_IDX, INVALID_IDX, INVALID_IDX}, {}, 0, nullptr});
        Index f = static_cast<Index>(faces.size() - 1);
        if (vertices[v0].face == INVALID_IDX) vertices[v0].face = f;
        if (vertices[v1].face == INVALID_IDX) vertices[v1].face = f;
        if (vertices[v2].face == INVALID_IDX) vertices[v2].face = f;
        return f;
    }

    void set_neighbor(Index f, int i, Index nf) { faces[f].neighbors[i] = nf; }

    void link_neighbors(Index f1, int i1, Index f2, int i2) {
        faces[f1].neighbors[i1] = f2;
        faces[f2].neighbors[i2] = f1;
    }

    Index edge_origin(Index f, int i) const { return faces[f].vertices[i]; }
    Index edge_dest(Index f, int i) const { return faces[f].vertices[(i + 1) % 3]; }
    Index neighbor(Index f, int i) const { return faces[f].neighbors[i]; }
    Index opposite_vertex(Index f, int i) const { return faces[f].vertices[(i + 2) % 3]; }

    std::vector<Index> star_hole(const std::vector<std::pair<Index, int>>& border_edges, Index new_vertex) {
        std::vector<Index> new_faces;
        const size_t m = border_edges.size();
        if (m == 0) return new_faces;
        new_faces.reserve(m);

        for (size_t j = 0; j < m; ++j) {
            Index f_old = border_edges[j].first;
            int i_old = border_edges[j].second;
            Index v0 = edge_origin(f_old, i_old);
            Index v1 = edge_dest(f_old, i_old);
            Index nf = add_face(v0, v1, new_vertex);
            new_faces.push_back(nf);
        }

        // [核心修复]: 将新面正确链接到孔洞外侧存活的面 (f_inv)
        for (size_t j = 0; j < m; ++j) {
            Index f_old = border_edges[j].first;
            int   i_old = border_edges[j].second;
            Index nf = new_faces[j];

            Index f_inv = neighbor(f_old, i_old);
            if (f_inv != INVALID_IDX) {
                int i_inv = -1;
                for (int k = 0; k < 3; ++k) {
                    if (faces[f_inv].neighbors[k] == f_old) {
                        i_inv = k;
                        break;
                    }
                }
                if (i_inv != -1) {
                    set_neighbor(nf, 0, f_inv);
                    set_neighbor(f_inv, i_inv, nf);
                }
            }
        }

        for (size_t j = 0; j < m; ++j) {
            Index cur  = new_faces[j];
            Index prev = new_faces[(j + m - 1) % m];
            set_neighbor(prev, 1, cur);
            set_neighbor(cur,  2, prev);
        }

        vertices[new_vertex].face = new_faces[0];
        return new_faces;
    }

    void delete_face(Index f) { faces[f].info = -1; }

    const Point3D<T>& point(Index vertex_idx) const { return (*points_ptr)[vertices[vertex_idx].point_idx]; }
    Index point_index(Index vertex_idx) const { return vertices[vertex_idx].point_idx; }
};

} // namespace StuCanvas