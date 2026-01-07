#include "../pch.h"
#include "../include/graph/GeoGraph.h"
#include "../include/graph/GeoFactory.h"
#include "../include/plot/plotCall.h"
#include "../include/functions/lerp.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <iomanip>

// 辅助导出函数
void ExportPoints(const std::string& filename, const AlignedVector<PointData>& buffer) {
    std::ofstream outfile(filename);
    outfile << "# [X_Clip] [Y_Clip] [Func_ID]\n" << std::fixed << std::setprecision(6);
    for (const auto& pt : buffer) {
        outfile << pt.position.x << " " << pt.position.y << " " << pt.function_index << "\n";
    }
    outfile.close();
}

int main() {
    try {
        // =========================================================
        // 1. 【严格执行】用户指定的初始视图参数
        // =========================================================
        double screen_width = 2560.0;
        double screen_height = 1600.0;
        double offset_x = 0.0;
        double offset_y = 0.0;
        double zoom = 0.1;

        double aspect_ratio = screen_width / screen_height;
        double wppx = (2.0 * aspect_ratio) / (zoom * screen_width);
        double wppy = -2.0 / (zoom * screen_height);

        Vec2 world_origin = {
            offset_x - (screen_width * 0.5) * wppx,
            offset_y - (screen_height * 0.5) * wppy
        };

        ViewState view{};
        view.screen_width = screen_width;
        view.screen_height = screen_height;
        view.offset_x = offset_x;
        view.offset_y = offset_y;
        view.zoom = zoom;
        view.world_origin = world_origin;
        view.wppx = wppx;
        view.wppy = wppy;

        using namespace GeoFactory;
        GeometryGraph graph;



    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
    return 0;
}