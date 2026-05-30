#pragma once

#include <fstream>
#include <filesystem>
#include <string>
#include <algorithm>
#include <cmath>
#include "../../types/point.hpp"
#include "../../types/path.hpp"

namespace StuCanvas::Export {

template <typename T>
bool ToSVG(const std::filesystem::path& filepath,
           const Path2D<T>& path,
           const std::string& stroke_color = "black",
           float stroke_width = 2.0f)
{
    if (path.control_points.size() < 4) return false;

    std::ofstream out(filepath);
    if (!out.is_open()) return false;

    // 计算包围盒用于 viewBox
    T min_x = path.control_points[0].x;
    T max_x = min_x;
    T min_y = path.control_points[0].y;
    T max_y = min_y;
    for (const auto& p : path.control_points) {
        min_x = std::min(min_x, p.x);
        max_x = std::max(max_x, p.x);
        min_y = std::min(min_y, p.y);
        max_y = std::max(max_y, p.y);
    }

    T width  = max_x - min_x;
    T height = max_y - min_y;
    T margin = std::max(width, height) * T(0.1);

    // [修复2] 防止图形是一条水平/垂直直线时，宽高为0导致 viewBox 无效或被裁切
    if (margin <= T(1e-5)) {
        margin = static_cast<T>(stroke_width) * T(5.0);
    }

    min_x -= margin;
    min_y -= margin;
    width  += margin * T(2);
    height += margin * T(2);

    // 精确输出控制，避免科学计数法破坏SVG语法
    out << std::fixed;
    out.precision(3);

    // SVG 头部
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" "
        << "viewBox=\"" << min_x << " " << min_y << " " << width << " " << height << "\">\n";

    // 路径开始
    out << "  <path d=\"";

    // [修复1] 严格匹配 4N 格式规范：每 4 个点为一段完整的三次贝塞尔
    for (size_t i = 0; i + 3 < path.control_points.size(); i += 4) {
        const auto& p0 = path.control_points[i];
        const auto& c1 = path.control_points[i + 1];
        const auto& c2 = path.control_points[i + 2];
        const auto& p1 = path.control_points[i + 3];

        // 决定是否需要发出新的 MoveTo 命令
        if (i == 0) {
            out << "M " << p0.x << " " << p0.y;
        } else {
            // 如果这一段的起点，和上一段的终点不重合（断笔），则重新 M
            const auto& prev_p1 = path.control_points[i - 1];
            if (std::abs(p0.x - prev_p1.x) > T(1e-4) || std::abs(p0.y - prev_p1.y) > T(1e-4)) {
                out << " M " << p0.x << " " << p0.y;
            }
        }

        // 生成三次贝塞尔指令 CurveTo
        out << " C " << c1.x << " " << c1.y << " "
            << c2.x << " " << c2.y << " "
            << p1.x << " " << p1.y;
    }

    // [修复3] 增加 linecap 和 linejoin 让线条端点和拐角变圆滑，彻底消除生硬感
    out << "\" fill=\"none\" "
        << "stroke=\"" << stroke_color << "\" "
        << "stroke-width=\"" << stroke_width << "\" "
        << "stroke-linecap=\"round\" "
        << "stroke-linejoin=\"round\" />\n";

    out << "</svg>\n";

    return true;
}

} // namespace StuCanvas::Export