// =============================================================================
// 🚀 main.cpp (完美对齐 VulkanQueue 托管相机的极致简链路测试)
// =============================================================================
#define VMA_IMPLEMENTATION


#include <iostream>
#include <span>
#include <vector>

#include "../canvas/queue.hpp"
#include "../objects/dag/graph.hpp"
using namespace StuCanvas;


int main ()
{


    try
    {
        std::cout << "正在初始化 Vulkan 纯无头（Headless）管线...\n";

        VulkanQueue queue;


        int bbbbbbabbbbbbbbbbbbba;

        AppearanceSimplePoint app{};


        // 分配 1080p 离屏渲染画布目标
        uint32_t slot = queue.createImage ( 2560, 1600 );
        std::cout << "成功分配离屏渲染目标，Slot 索引为: " << slot << "\n";

        // =====================================================================
        // 🚀 1. 核心改进：直接调用成员函数创建透视相机，零手动内存分配
        // =====================================================================
        // 成员函数内部会自动 new 物理相机，用 Camera 包装并塞入 cameras 容器托管 [1]
        PerspectiveCamera& camera_ref = queue.createPerspectiveCamera ();

        // 直接通过引用对内/外参进行赋值
        //
        camera_ref.position[ 0 ] = 0.0;
        camera_ref.position[ 1 ] = 0.0;

        camera_ref.position[ 2 ] = 5.0;

        camera_ref.target[ 0 ] = 0.0;
        camera_ref.target[ 1 ] = 0.0;
        camera_ref.target[ 2 ] = 0.0;


        camera_ref.up[ 0 ] = 0.0;
        camera_ref.up[ 1 ] = 1.0;
        camera_ref.up[ 2 ] = 0.0;

        camera_ref.near_clip = 0.1;
        camera_ref.far_clip = 100.0;
        camera_ref.fov_y = 45.0;
        camera_ref.aspect = 1920.0 / 1080.0;

        // =====================================================================
        // 2. 构建几何场景：1 个原点处的自由点 [1]
        // =====================================================================
        std::cout << "正在构建 3D 物理场景图...\n";

        DAGObject point_obj{};
        point_obj.type = NodeType::POINT_3D_FREE;
        point_obj.data.point_3d.x = 0.0;

        point_obj.data.point_3d.y = 0.0;


        point_obj.data.point_3d.z = 0.0;


        // 创建该点的物理放置实例
        DAGObjectInstance point_inst{};
        point_inst.source = &point_obj;

        point_inst.world_position[ 0 ] = 0.0;
        point_inst.world_position[ 1 ] = 0.0;
        point_inst.world_position[ 2 ] = 0.0;


        point_inst.world_rotation[ 0 ] = 0.0;
        point_inst.world_rotation[ 1 ] = 0.0;
        point_inst.world_rotation[ 2 ] = 0.0;
        point_inst.world_rotation[ 3 ] = 1.0;


        point_inst.world_scales[ 0 ] = 1.0;
        point_inst.world_scales[ 1 ] = 1.0;
        point_inst.world_scales[ 2 ] = 1.0;

        point_inst.appearance = &app;   // 自动挂载默认外观

        // 必须登记到 Object 自身维护的 instances 列表中 [1]
        point_obj.instances.push_back ( &point_inst );

        // 收集待渲染实例
        std::vector< DAGObjectInstance* > instances_to_render = { &point_inst };

        // =====================================================================
        // 3. 🚀 执行 NLE 动作渲染：一键录制、提交、与安全导出
        // =====================================================================
        std::cout << "正在录制图像指令...\n";

        // 🚀 核心修改点：
        // 提取 cameras 容器中的最后一项，即为对应我们刚刚配置好的那个通用 Camera 包装器
        Camera& generic_camera = queue.cameras[ 0 ];

        queue.recordImage_DAGInst ( instances_to_render, slot, generic_camera );

        std::cout << "正在提交 GPU 并等待渲染完成...\n";
        queue.submitGraphics ();

        std::cout << "正在从显卡物理回读并导出 PNG 文件...\n";
        std::string out_file = "point_cloud_output.png";
        queue.exportImage ( slot, out_file );

        std::cout << "🎉 测试成功！画面已完好写入到: " << out_file << "\n";
    }
    catch ( const std::exception& e )
    {
        std::cerr << "❌ 运行时致命错误: " << e.what () << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
