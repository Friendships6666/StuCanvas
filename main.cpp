#include <iostream>
#include <vector>
#include <cmath>
#include <fstream>
#include <limits>
#include <algorithm>
#include <stdexcept>

#include "stucanvas/plot/implicit_2d.hpp"
#include "stucanvas/reconstruction/reconstruct.hpp"
#include "stucanvas/reconstruction/bezier_fitting.hpp"   // 包含无压缩简单拟合器
#include "stucanvas/types/segment_strip.hpp"
#include "stucanvas/types/path.hpp"

// 辅助包围盒：用于自动调整SVG viewBox
struct BBox {
    double min_x = std::numeric_limits<double>::max();
    double max_x = std::numeric_limits<double>::lowest();
    double min_y = std::numeric_limits<double>::max();
    double max_y = std::numeric_limits<double>::lowest();

    void add_point(double x, double y) {
        if (x < min_x) min_x = x;
        if (x > max_x) max_x = x;
        if (y < min_y) min_y = y;
        if (y > max_y) max_y = y;
    }

    void add_margin(double margin = 1.0) {
        min_x -= margin; max_x += margin;
        min_y -= margin; max_y += margin;
    }

    std::string viewBox() const {
        double w = max_x - min_x;
        double h = max_y - min_y;
        return std::to_string(min_x) + " " + std::to_string(min_y) + " " +
               std::to_string(w) + " " + std::to_string(h);
    }
};

int main()
{
    // ---------- 1. 隐函数绘图 ----------
    std::vector<StuCanvas::Point2D<double>> raw_points;
    auto f = [](auto x, auto y) { return cos(y)+sin(x) - 0.5; };

    StuCanvas::IntervalPlot2DDescriptor<double, decltype(f)> desc;
    desc.function            = f;
    desc.result              = &raw_points;
    desc.x_min               = -50.0;
    desc.x_max               =  50.0;
    desc.y_min               = -50.0;
    desc.y_max               =  50.0;
    desc.max_recursion_depth = 200;
    desc.sampling_threshold  = 0.01;
    desc.use_de_refinement   = true;
    desc.de_population_size  = 200;
    desc.de_max_generations  = 20;
    desc.cpu_threads         = 0;

    StuCanvas::plot_interval_2D(desc);
    std::cout << "[INFO] Generated " << raw_points.size() << " raw points.\n";

    // ---------- 2. 流形重建 ----------
    double pixel_size = 0.2;
    std::vector<StuCanvas::SegmentStrip2D<double>> strips;
    try {
        strips = StuCanvas::reconstruction::reconstruct_from_points(raw_points, pixel_size);
    } catch (const std::exception& e) {
        std::cerr << "[FATAL] Reconstruction exception: " << e.what() << std::endl;
        return 1;
    }
    if (strips.empty()) {
        std::cerr << "[FATAL] No strips were produced by reconstruction. Exiting.\n";
        return 1;
    }

    // 计算全局包围盒（基于原始点云）
    BBox bbox;
    for (const auto& p : raw_points) {
        bbox.add_point(p.x, p.y);
    }
    bbox.add_margin(1.0);   // 留出一点边距
    std::string viewBox = bbox.viewBox();

    // ---------- 3. 导出原始点云 SVG ----------
    {
        std::ofstream svg("points.svg");
        svg << R"(<svg xmlns="http://www.w3.org/2000/svg" width="400" height="400" viewBox=")"
            << viewBox << "\">\n";
        for (const auto& p : raw_points) {
            svg << "  <circle cx=\"" << p.x << "\" cy=\"" << p.y
                << "\" r=\"0.2\" fill=\"red\"/>\n";
        }
        svg << "</svg>\n";
        std::cout << "[OK] Raw points SVG written to points.svg\n";
    }

    // ---------- 4. 导出重建折线段 SVG ----------
    {
        std::ofstream svg("segments.svg");
        svg << R"(<svg xmlns="http://www.w3.org/2000/svg" width="400" height="400" viewBox=")"
            << viewBox << "\">\n";
        for (const auto& strip : strips) {
            if (strip.vertices.size() < 2) continue;
            // 用 <polyline> 绘制开放折线
            std::string pts_str;
            for (const auto& v : strip.vertices) {
                pts_str += std::to_string(v.x) + "," + std::to_string(v.y) + " ";
            }
            svg << "  <polyline points=\"" << pts_str
                << "\" fill=\"none\" stroke=\"green\" stroke-width=\"0.1\"/>\n";

            // 闭合曲线需补画最后一段
            if (strip.closed && strip.vertices.size() >= 2) {
                svg << "  <line x1=\"" << strip.vertices.back().x
                    << "\" y1=\"" << strip.vertices.back().y
                    << "\" x2=\"" << strip.vertices.front().x
                    << "\" y2=\"" << strip.vertices.front().y
                    << "\" stroke=\"green\" stroke-width=\"0.1\"/>\n";
            }
        }
        svg << "</svg>\n";
        std::cout << "[OK] Line segments SVG written to segments.svg\n";
    }

    // ---------- 5. 拟合贝塞尔曲线并导出 SVG ----------
    {
        std::ofstream svg("beziers.svg");
        svg << R"(<svg xmlns="http://www.w3.org/2000/svg" width="400" height="400" viewBox=")"
            << viewBox << "\">\n";

        int path_count = 0;
        for (const auto& strip : strips) {
            if (strip.vertices.size() < 2) continue;

            // 调用自有 Catmull‑Rom 拟合
            auto path = StuCanvas::reconstruction::fit_cubic_bezier_simple(strip, 0.5);
            const auto& ctrl = path.control_points;
            if (ctrl.size() < 4 || ctrl.size() % 4 != 0) {
                std::cerr << "[WARN] Invalid control point count, skipping strip.\n";
                continue;
            }

            // 生成 SVG path d 属性
            std::string path_d;
            for (size_t i = 0; i < ctrl.size(); i += 4) {
                if (i == 0) {
                    path_d += "M " + std::to_string(ctrl[i].x) + " " +
                              std::to_string(ctrl[i].y) + " ";
                }
                path_d += "C "
                          + std::to_string(ctrl[i+1].x) + " " + std::to_string(ctrl[i+1].y) + " "
                          + std::to_string(ctrl[i+2].x) + " " + std::to_string(ctrl[i+2].y) + " "
                          + std::to_string(ctrl[i+3].x) + " " + std::to_string(ctrl[i+3].y) + " ";
            }
            if (strip.closed) {
                path_d += "Z";
            }

            svg << "  <path d=\"" << path_d
                << "\" fill=\"none\" stroke=\"blue\" stroke-width=\"0.2\"/>\n";
            ++path_count;
        }

        svg << "</svg>\n";
        std::cout << "[OK] Bezier curves SVG written to beziers.svg ("
                  << path_count << " paths)\n";
    }

    return 0;
}