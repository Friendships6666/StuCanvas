// include/stucanvas/reconstruction/quick_hull_3d.hpp
#pragma once

#include <vector>
#include <list>
#include <queue>
#include <map>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <iostream> // For logging
#include <sstream>

#include "../types/point.hpp"
#include "../types/mesh.hpp"
#include "../types/convex_hull_ds.hpp"
#include "quick_hull_2d.hpp" // 确保路径正确

namespace StuCanvas {

template <typename T>
class QuickHull3D {
public:
    using Index = uint32_t;
    using Point3 = Point3D<T>;
    using Mesh3  = Mesh3D<T>;

    Mesh3 Compute(const std::vector<Point3>& points) {
        if (points.size() < 3) return Mesh3{};

        // 如果点数小于 4，理论上无法构成 3D 体，直接尝试 2D
        if (points.size() < 4) {
            return QuickHull2D<T>().Compute(points);
        }

        return compute_hull(points);
    }

private:
    ConvexHullDS3D<T> hull;
    const std::vector<Point3>* pts = nullptr;

    T global_tol = 1e-5;

    // 字典序决胜：在距离相同时，强制选择坐标字典序最大的点，必定滑向绝对角点
    bool lexicographically_greater(const Point3& p, const Point3& ref) const {
        if (p.x > ref.x + global_tol) return true;
        if (std::abs(p.x - ref.x) <= global_tol) {
            if (p.y > ref.y + global_tol) return true;
            if (std::abs(p.y - ref.y) <= global_tol && p.z > ref.z + global_tol) return true;
        }
        return false;
    }

    std::vector<typename std::list<Index>::iterator> face_iter;

    static constexpr T EPS = static_cast<T>(1e-9);

    static std::string pt_str(const Point3& p) {
        std::ostringstream oss;
        oss << "(" << p.x << ", " << p.y << ", " << p.z << ")";
        return oss.str();
    }

    static Point3 sub(const Point3& a, const Point3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
    static T dot(const Point3& a, const Point3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    static Point3 cross(const Point3& a, const Point3& b) {
        return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    }
    static T length_sq(const Point3& v) { return dot(v, v); }
    static T length(const Point3& v) { return std::sqrt(length_sq(v)); }

    static bool is_positive_side(const Point3& plane_normal, const Point3& plane_point, const Point3& query) {
        return dot(plane_normal, sub(query, plane_point)) > EPS;
    }

    Point3 compute_normal(Index fi) const {
        const auto& f = hull.faces[fi];
        Point3 a = pts->at(hull.point_index(f.vertices[0]));
        Point3 b = pts->at(hull.point_index(f.vertices[1]));
        Point3 c = pts->at(hull.point_index(f.vertices[2]));
        return cross(sub(b, a), sub(c, a));
    }

    Mesh3 compute_hull(const std::vector<Point3>& points) {
        pts = &points;
        hull.bind_points(points);

        if (!build_initial_tetrahedron()) {
            std::cout << "[QuickHull3D] Degenerate case detected. Falling back to QuickHull2D.\n";
            return QuickHull2D<T>().Compute(points);
        }
        unassigned_points.assign(points.size(), true);
        for (Index vi : initial_vertices) { unassigned_points[vi] = false; }

        std::list<Index> remaining_points;
        for (size_t i = 0; i < points.size(); ++i) {
            if (unassigned_points[i]) remaining_points.push_back(static_cast<Index>(i));
        }

        // std::cout << "[QuickHull3D] Assigning " << remaining_points.size() << " remaining points to initial faces...\n";
        assign_points_to_faces(initial_faces, remaining_points);

        std::list<Index> pending_faces;
        face_iter.resize(hull.faces.size(), pending_faces.end());

        for (Index fi : initial_faces) {
            if (!hull.faces[fi].outside_points.empty()) {
                pending_faces.push_back(fi);
                face_iter[fi] = std::prev(pending_faces.end());
            }
        }

        int iter_count = 0;
        while (!pending_faces.empty()) {
            iter_count++;
            Index f = pending_faces.front();
            pending_faces.pop_front();
            face_iter[f] = pending_faces.end();

            auto& face = hull.faces[f];
            if (face.info == -1) continue; // Skip if it was deleted
            if (face.outside_points.empty()) continue;

            Index eye_pt_idx = farthest_point(f);
            if (eye_pt_idx == INVALID_INDEX) continue;

            // std::cout << "[QuickHull3D] Iteration " << iter_count << " | Processing face " << f
            //           << " | Farthest point idx: " << eye_pt_idx << " " << pt_str(pts->at(eye_pt_idx)) << "\n";

            remove_from_list(face.outside_points, eye_pt_idx);

            std::vector<Index> visible_faces;
            std::vector<std::pair<Index, int>> border_edges;
            find_visible_set(f, eye_pt_idx, visible_faces, border_edges);

            // std::cout << "  -> Found " << visible_faces.size() << " visible faces, "
            //           << border_edges.size() << " border edges.\n";

            if (border_edges.empty()) {
                std::cerr << "  -> WARNING: No border edges found! Skipping point.\n";
                continue;
            }

            std::list<Index> global_outside;

            // =================================================================
            // [极其关键的修复]：遍历所有的可见面，收集它们的外部点，并全部标记为删除
            // 绝不能跳过起步面 f，f 也必须被删除！
            // 之前的 `if (vf == f) continue;` 导致了严重的内穿插拓扑错误。
            // =================================================================
            for (Index vf : visible_faces) {
                auto& vface = hull.faces[vf];
                if (!vface.outside_points.empty()) {
                    global_outside.splice(global_outside.end(), vface.outside_points);
                }
                if (face_iter[vf] != pending_faces.end()) {
                    pending_faces.erase(face_iter[vf]);
                    face_iter[vf] = pending_faces.end();
                }
                vface.info = -1; // 彻底标记可见面（包含f）为已删除
            }

            Index new_vertex = hull.add_vertex(eye_pt_idx);

            std::vector<Index> new_faces = hull.star_hole(border_edges, new_vertex);
            face_iter.resize(hull.faces.size(), pending_faces.end());

            // std::cout << "  -> Created " << new_faces.size() << " new faces.\n";

            assign_points_to_faces(new_faces, global_outside);

            for (Index nf : new_faces) {
                if (!hull.faces[nf].outside_points.empty()) {
                    pending_faces.push_back(nf);
                    face_iter[nf] = std::prev(pending_faces.end());
                }
            }
        }

        // std::cout << "[QuickHull3D] Core algorithm finished after " << iter_count << " iterations.\n";
        return extract_mesh();
    }

    std::vector<Index> initial_vertices;
    std::vector<Index> initial_faces;
    std::vector<bool> unassigned_points;

bool build_initial_tetrahedron() {
        const auto& points = *pts;
        size_t n = points.size();
        if (n < 4) return false;

        // 0) 根据点云包围盒动态计算鲁棒误差 (极大增强浮点稳定性)
        T min_x = points[0].x, max_x = points[0].x;
        T min_y = points[0].y, max_y = points[0].y;
        T min_z = points[0].z, max_z = points[0].z;
        for (const auto& p : points) {
            if (p.x < min_x) min_x = p.x;
            if (p.x > max_x) max_x = p.x;
            if (p.y < min_y) min_y = p.y;
            if (p.y > max_y) max_y = p.y;
            if (p.z < min_z) min_z = p.z;
            if (p.z > max_z) max_z = p.z;
        }
        T max_span = std::max({max_x - min_x, max_y - min_y, max_z - min_z});
        global_tol = max_span * static_cast<T>(1e-5);
        if (global_tol < EPS) global_tol = EPS;

        // 辅助 Lambda: 带误差吸收和字典序的极小值判定
        auto tie_break_min = [&](T val, T ref, T sec, T ref_sec, T thd, T ref_thd) {
            if (val < ref - global_tol) return true;
            if (std::abs(val - ref) <= global_tol) {
                if (sec < ref_sec - global_tol) return true;
                if (std::abs(sec - ref_sec) <= global_tol && thd < ref_thd - global_tol) return true;
            }
            return false;
        };

        // 辅助 Lambda: 带误差吸收和字典序的极大值判定
        auto tie_break_max = [&](T val, T ref, T sec, T ref_sec, T thd, T ref_thd) {
            if (val > ref + global_tol) return true;
            if (std::abs(val - ref) <= global_tol) {
                if (sec > ref_sec + global_tol) return true;
                if (std::abs(sec - ref_sec) <= global_tol && thd > ref_thd + global_tol) return true;
            }
            return false;
        };

        // 1) 寻找六个极值点 (强制滑向绝对角点)
        Index extremes[6] = {0,0,0,0,0,0};
        for (size_t i = 1; i < n; ++i) {
            const Point3& p = points[i];

            const Point3& e0 = points[extremes[0]];
            if (tie_break_min(p.x, e0.x, p.y, e0.y, p.z, e0.z)) extremes[0] = i;

            const Point3& e1 = points[extremes[1]];
            if (tie_break_max(p.x, e1.x, p.y, e1.y, p.z, e1.z)) extremes[1] = i;

            const Point3& e2 = points[extremes[2]];
            if (tie_break_min(p.y, e2.y, p.z, e2.z, p.x, e2.x)) extremes[2] = i;

            const Point3& e3 = points[extremes[3]];
            if (tie_break_max(p.y, e3.y, p.z, e3.z, p.x, e3.x)) extremes[3] = i;

            const Point3& e4 = points[extremes[4]];
            if (tie_break_min(p.z, e4.z, p.x, e4.x, p.y, e4.y)) extremes[4] = i;

            const Point3& e5 = points[extremes[5]];
            if (tie_break_max(p.z, e5.z, p.x, e5.x, p.y, e5.y)) extremes[5] = i;
        }

        // 2) 从这六个点中找出最远点对 (作为前两个顶点)
        Index v0 = 0, v1 = 0;
        T max_dist_sq = -1;
        for (int i = 0; i < 6; ++i) {
            for (int j = i+1; j < 6; ++j) {
                Index a = extremes[i], b = extremes[j];
                if (a == b) continue;
                T dsq = length_sq(sub(points[a], points[b]));
                if (dsq > max_dist_sq) {
                    max_dist_sq = dsq;
                    v0 = a; v1 = b;
                }
            }
        }
        if (max_dist_sq < EPS) return false;

        // 3) 寻找第三个点：距离直线 (v0,v1) 最远的点，结合字典序决胜
        Point3 dir = sub(points[v1], points[v0]);
        T dir_len_sq = length_sq(dir);
        Index v2 = INVALID_INDEX;
        T max_line_dist_sq = -1;
        for (size_t i = 0; i < n; ++i) {
            if (i == v0 || i == v1) continue;
            T d_sq = (dir_len_sq > EPS) ? length_sq(cross(sub(points[i], points[v0]), dir)) / dir_len_sq
                                        : length_sq(sub(points[i], points[v0]));

            if (d_sq > max_line_dist_sq + global_tol) {
                max_line_dist_sq = d_sq;
                v2 = i;
            } else if (std::abs(d_sq - max_line_dist_sq) <= global_tol && v2 != INVALID_INDEX) {
                if (lexicographically_greater(points[i], points[v2])) v2 = i;
            }
        }
        if (v2 == INVALID_INDEX || max_line_dist_sq < EPS) return false;

        // 4) 寻找第四个点：距离平面 (v0,v1,v2) 最远的点，结合字典序决胜
        Point3 normal = cross(sub(points[v1], points[v0]), sub(points[v2], points[v0]));
        T normal_len = length(normal);
        if (normal_len < EPS) return false;
        normal.x /= normal_len; normal.y /= normal_len; normal.z /= normal_len;

        Index v3 = INVALID_INDEX;
        T max_plane_dist = -1;
        for (size_t i = 0; i < n; ++i) {
            if (i == v0 || i == v1 || i == v2) continue;
            T d = std::abs(dot(normal, sub(points[i], points[v0])));

            if (d > max_plane_dist + global_tol) {
                max_plane_dist = d;
                v3 = i;
            } else if (std::abs(d - max_plane_dist) <= global_tol && v3 != INVALID_INDEX) {
                if (lexicographically_greater(points[i], points[v3])) v3 = i;
            }
        }
        if (v3 == INVALID_INDEX || max_plane_dist < EPS) return false;

        // std::cout << "[QuickHull3D] Initial tetrahedron vertices: \n"
        //           << "  v0: " << pt_str(points[v0]) << "\n"
        //           << "  v1: " << pt_str(points[v1]) << "\n"
        //           << "  v2: " << pt_str(points[v2]) << "\n"
        //           << "  v3: " << pt_str(points[v3]) << "\n";

        // 5) 确保四面体方向朝外
        if (dot(normal, sub(points[v3], points[v0])) > 0) {
            // std::cout << "  -> Flipped initial face orientation to point outwards.\n";
            std::swap(v2, v1);
        }

        hull = ConvexHullDS3D<T>();
        hull.bind_points(*pts);
        Index h_v0 = hull.add_vertex(v0);
        Index h_v1 = hull.add_vertex(v1);
        Index h_v2 = hull.add_vertex(v2);
        Index h_v3 = hull.add_vertex(v3);

        Index f0 = hull.add_face(h_v0, h_v1, h_v2);
        Index f1 = hull.add_face(h_v3, h_v1, h_v0);
        Index f2 = hull.add_face(h_v3, h_v2, h_v1);
        Index f3 = hull.add_face(h_v3, h_v0, h_v2);

        hull.set_neighbor(f0, 0, f1);
        hull.set_neighbor(f0, 1, f2);
        hull.set_neighbor(f0, 2, f3);

        hull.set_neighbor(f1, 0, f2);
        hull.set_neighbor(f1, 1, f0);
        hull.set_neighbor(f1, 2, f3);

        hull.set_neighbor(f2, 0, f3);
        hull.set_neighbor(f2, 1, f0);
        hull.set_neighbor(f2, 2, f1);

        hull.set_neighbor(f3, 0, f1);
        hull.set_neighbor(f3, 1, f0);
        hull.set_neighbor(f3, 2, f2);

        hull.vertices[h_v0].face = f0;
        hull.vertices[h_v1].face = f0;
        hull.vertices[h_v2].face = f0;
        hull.vertices[h_v3].face = f1;

        initial_faces = {f0, f1, f2, f3};
        initial_vertices = {v0, v1, v2, v3};

        return true;
    }

    void assign_points_to_faces(const std::vector<Index>& face_list, std::list<Index>& point_set) {
        for (auto pt_it = point_set.begin(); pt_it != point_set.end(); ) {
            Index pt_idx = *pt_it;
            const Point3& p = pts->at(pt_idx);
            Index best_face = INVALID_INDEX;
            T max_dist = -1;

            for (Index fi : face_list) {
                if (hull.faces[fi].info == -1) continue;
                Point3 n = compute_normal(fi);
                T dist = dot(n, sub(p, pts->at(hull.point_index(hull.faces[fi].vertices[0]))));
                if (dist > max_dist) {
                    max_dist = dist;
                    best_face = fi;
                }
            }
            if (best_face != INVALID_INDEX && max_dist > EPS) {
                hull.faces[best_face].outside_points.push_back(pt_idx);
                pt_it = point_set.erase(pt_it);
            } else {
                ++pt_it;
            }
        }
    }

    Index farthest_point(Index f) {
    const auto& outside = hull.faces[f].outside_points;
    if (outside.empty()) return INVALID_INDEX;
    Point3 plane_p = pts->at(hull.point_index(hull.faces[f].vertices[0]));
    Point3 n = compute_normal(f);
    Index best_idx = INVALID_INDEX;
    T max_dist = -1;
    for (Index pi : outside) {
        const Point3& p = pts->at(pi);
        T dist = dot(n, sub(p, plane_p));
        if (dist > max_dist + global_tol) {
            max_dist = dist;
            best_idx = pi;
        } else if (std::abs(dist - max_dist) <= global_tol && best_idx != INVALID_INDEX) {
            if (lexicographically_greater(p, pts->at(best_idx))) {
                best_idx = pi;
            }
        }
    }
    return best_idx;
}

    void remove_from_list(std::list<Index>& lst, Index val) {
        for (auto it = lst.begin(); it != lst.end(); ++it) {
            if (*it == val) {
                lst.erase(it);
                return;
            }
        }
    }

    void find_visible_set(Index start_face, Index eye_pt_idx,
                          std::vector<Index>& visible,
                          std::vector<std::pair<Index, int>>& border_edges) {
        const Point3& eye = pts->at(eye_pt_idx);
        visible.clear();
        border_edges.clear();

        std::vector<int> visited(hull.faces.size(), 0);
        std::queue<Index> q;
        q.push(start_face);
        visited[start_face] = 1;
        visible.push_back(start_face);

        while (!q.empty()) {
            Index cf = q.front(); q.pop();
            for (int i = 0; i < 3; ++i) {
                Index nf = hull.faces[cf].neighbors[i];
                if (nf == ConvexHullDS3D<T>::INVALID_IDX) continue;
                if (hull.faces[nf].info == -1) continue;

                if (visited[nf] == 0) {
                    Point3 nn = compute_normal(nf);
                    T dist = dot(nn, sub(eye, pts->at(hull.point_index(hull.faces[nf].vertices[0]))));
                    if (dist > EPS) {
                        visited[nf] = 1;
                        visible.push_back(nf);
                        q.push(nf);
                    } else {
                        visited[nf] = 2;
                        border_edges.push_back({cf, i});
                    }
                } else if (visited[nf] == 2) {
                    border_edges.push_back({cf, i});
                }
            }
        }

        std::map<Index, std::pair<Index, int>> vertex_to_edge;
        size_t original_be_size = border_edges.size();
        for (const auto& be : border_edges) {
            Index f = be.first;
            int i = be.second;
            Index v_start = hull.edge_origin(f, i);
            vertex_to_edge[v_start] = be;
        }

        border_edges.clear();
        if (vertex_to_edge.empty()) return;

        Index start_v = vertex_to_edge.begin()->first;
        Index cur_v = start_v;
        const size_t max_steps = vertex_to_edge.size() * 2;

        for (size_t step = 0; step < max_steps; ++step) {
            auto it = vertex_to_edge.find(cur_v);
            if (it == vertex_to_edge.end()) {
                std::cerr << "  -> ERROR: Border loop broken at vertex " << cur_v << "\n";
                break;
            }
            const auto& be = it->second;
            border_edges.push_back(be);
            Index f = be.first;
            int i = be.second;
            cur_v = hull.edge_dest(f, i);
            if (cur_v == start_v) break;
        }

        if (border_edges.size() != vertex_to_edge.size() || vertex_to_edge.size() != original_be_size) {
            std::cerr << "  -> ERROR: Invalid border loop! Orig size: " << original_be_size
                      << ", Map: " << vertex_to_edge.size() << ", Sorted: " << border_edges.size() << "\n";
            border_edges.clear();
        }
    }

    Mesh3 extract_mesh() {
        Mesh3 result;
        std::vector<Index> used_vertex_map(hull.vertices.size(), INVALID_INDEX);
        Index cur_vi = 0;

        int alive_faces = 0;
        for (size_t fi = 0; fi < hull.faces.size(); ++fi) {
            if (hull.faces[fi].info != -1) {
                alive_faces++;
                for (int k = 0; k < 3; ++k) {
                    Index vi = hull.faces[fi].vertices[k];
                    if (used_vertex_map[vi] == INVALID_INDEX) {
                        used_vertex_map[vi] = cur_vi++;
                        const Point3& p = pts->at(hull.point_index(vi));
                        result.vertices.push_back(Vertex3D<T>(p));
                    }
                }
            }
        }

        for (size_t fi = 0; fi < hull.faces.size(); ++fi) {
            if (hull.faces[fi].info != -1) {
                Index i0 = used_vertex_map[hull.faces[fi].vertices[0]];
                Index i1 = used_vertex_map[hull.faces[fi].vertices[1]];
                Index i2 = used_vertex_map[hull.faces[fi].vertices[2]];
                result.indices.push_back(i0);
                result.indices.push_back(i1);
                result.indices.push_back(i2);
            }
        }

        // std::cout << "[QuickHull3D] Extracted Mesh: " << result.vertices.size()
        //           << " vertices, " << alive_faces << " faces.\n";
        return result;
    }

    static constexpr Index INVALID_INDEX = std::numeric_limits<Index>::max();
};

} // namespace StuCanvas