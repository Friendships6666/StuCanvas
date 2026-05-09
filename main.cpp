// main.cpp
#include <iostream>
#include <vector>
#include <memory>
#include <array>
#include <chrono>

// 引入 StuCanvas Vulkan 封装组件
#include "stucanvas/canvas/vulkan/init.hpp"
#include "stucanvas/canvas/vulkan/swap_chains.hpp"
#include "stucanvas/canvas/vulkan/renderpass.hpp"
#include "stucanvas/canvas/vulkan/pipeline.hpp"
#include "stucanvas/canvas/vulkan/shader_module.hpp"
#include "stucanvas/canvas/vulkan/buffer.hpp"
#include "stucanvas/canvas/vulkan/present.hpp"
#include "stucanvas/types/point.hpp"
#include "stucanvas/types/triangles.hpp" // 引入三角形集合

using namespace StuCanvas::Vulkan;
using namespace StuCanvas;

// ── 顶点着色器 (Slang) ──────────────────────────────────────────
const char* vertexSlang = R"(
struct PushConstants {
    float time;
    float aspect;
};
[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

struct VSInput
{
    [[vk::location(0)]] float3 pos : POSITION;
    [[vk::location(1)]] float4 color : COLOR0;
};

struct VSOutput
{
    float4 pos : SV_Position;
    float3 worldPos : WORLDPOS;
    float4 color : COLOR0;
};

[shader("vertex")]
VSOutput main(VSInput input)
{
    VSOutput output;

    // 1. 基于时间计算旋转矩阵 (绕 X 和 Y 轴)
    float c = cos(pc.time * 0.8);
    float s = sin(pc.time * 0.8);
    float3x3 rotY = float3x3(c, 0, s,  0, 1, 0,  -s, 0, c);

    float c2 = cos(pc.time * 0.5);
    float s2 = sin(pc.time * 0.5);
    float3x3 rotX = float3x3(1, 0, 0,  0, c2, -s2,  0, s2, c2);

    // 应用旋转并向 Z 轴深处平移
    float3 worldP = mul(rotX, mul(rotY, input.pos));
    worldP.z += 4.0; // 将物体推离相机
    output.worldPos = worldP;

    // 2. 透视投影 (Perspective Projection)
    float fov = 1.047; // 约 60 度
    float near = 0.1;
    float far = 100.0;
    float fScale = 1.0 / tan(fov * 0.5);

    float4 projPos;
    projPos.x = worldP.x * (fScale / pc.aspect);
    projPos.y = -worldP.y * fScale;  // ⚠️ Vulkan 的 Y 轴向下，所以这里取反
    projPos.z = worldP.z * (far / (far - near)) - (far * near / (far - near));
    projPos.w = worldP.z;

    output.pos = projPos;
    output.color = input.color;

    return output;
}
)";

// ── 片段着色器 (Slang) ──────────────────────────────────────────
const char* fragmentSlang = R"(
struct PSInput
{
    float4 pos : SV_Position;
    float3 worldPos : WORLDPOS;
    float4 color : COLOR0;
};

[shader("fragment")]
float4 main(PSInput input) : SV_Target
{
    // 巧妙技巧：利用屏幕空间导数自动计算法线！(无需在顶点中传入法线)
    float3 dx = ddx(input.worldPos);
    float3 dy = ddy(input.worldPos);
    float3 normal = normalize(cross(dx, dy));

    // 定向光照模型
    float3 lightDir = normalize(float3(0.5, 1.0, -0.8));

    // 使用 abs() 实现双面光照，防止法线翻转时变黑
    float diffuse = max(abs(dot(normal, lightDir)), 0.15); // 0.15 为环境光底色

    float3 finalColor = input.color.rgb * diffuse;
    return float4(finalColor, input.color.a);
}
)";

// ── 辅助函数：构建立方体数据 ──
Triangles3D_GPU generateCube() {
    Triangles3D_GPU cube;

    // 闭包函数：添加一个四边形面 (逆时针顺序)
    auto addFace = [&](Point3D_GPU p0, Point3D_GPU p1, Point3D_GPU p2, Point3D_GPU p3) {
        uint32_t base = cube.points.size();
        cube.points.push_back(p0);
        cube.points.push_back(p1);
        cube.points.push_back(p2);
        cube.points.push_back(p3);
        // 分割为两个三角形: 0-1-2 和 0-2-3
        cube.indices.push_back(base + 0); cube.indices.push_back(base + 1); cube.indices.push_back(base + 2);
        cube.indices.push_back(base + 0); cube.indices.push_back(base + 2); cube.indices.push_back(base + 3);
    };

    // 为6个面定义6种纯色
    float r[4]={1,0,0,1}, g[4]={0,1,0,1}, b[4]={0,0,1,1};
    float y[4]={1,1,0,1}, m[4]={1,0,1,1}, c[4]={0,1,1,1};

    // 正面 (Z+)
    addFace( {-1,-1, 1, r[0],r[1],r[2],1}, { 1,-1, 1, r[0],r[1],r[2],1}, { 1, 1, 1, r[0],r[1],r[2],1}, {-1, 1, 1, r[0],r[1],r[2],1} );
    // 背面 (Z-)
    addFace( { 1,-1,-1, g[0],g[1],g[2],1}, {-1,-1,-1, g[0],g[1],g[2],1}, {-1, 1,-1, g[0],g[1],g[2],1}, { 1, 1,-1, g[0],g[1],g[2],1} );
    // 右面 (X+)
    addFace( { 1,-1, 1, b[0],b[1],b[2],1}, { 1,-1,-1, b[0],b[1],b[2],1}, { 1, 1,-1, b[0],b[1],b[2],1}, { 1, 1, 1, b[0],b[1],b[2],1} );
    // 左面 (X-)
    addFace( {-1,-1,-1, y[0],y[1],y[2],1}, {-1,-1, 1, y[0],y[1],y[2],1}, {-1, 1, 1, y[0],y[1],y[2],1}, {-1, 1,-1, y[0],y[1],y[2],1} );
    // 上面 (Y+)
    addFace( {-1, 1, 1, m[0],m[1],m[2],1}, { 1, 1, 1, m[0],m[1],m[2],1}, { 1, 1,-1, m[0],m[1],m[2],1}, {-1, 1,-1, m[0],m[1],m[2],1} );
    // 下面 (Y-)
    addFace( {-1,-1,-1, c[0],c[1],c[2],1}, { 1,-1,-1, c[0],c[1],c[2],1}, { 1,-1, 1, c[0],c[1],c[2],1}, {-1,-1, 1, c[0],c[1],c[2],1} );

    return cube;
}

int main()
{
    try {
        VulkanInit vulkanInit("StuCanvas 3D Cube", 800, 600, true);

        VkFormat swapchainFormat;
        {
            SwapChain tempSC(
                vulkanInit.getPhysicalDevice(), vulkanInit.getDevice(),
                vulkanInit.getSurface(), VK_NULL_HANDLE,
                vulkanInit.getGraphicsFamily(), vulkanInit.getPresentFamily(),
                vulkanInit.getWindow()
            );
            swapchainFormat = tempSC.getImageFormat();
        }

        RenderPass renderPass(vulkanInit.getDevice(), swapchainFormat);

        // 获取正方体的顶点和索引数据
        Triangles3D_GPU cubeData = generateCube();

        auto vertShader = ShaderModule::fromSlangSource(
            vulkanInit.getDevice(), vertexSlang, "vertex", "main");
        auto fragShader = ShaderModule::fromSlangSource(
            vulkanInit.getDevice(), fragmentSlang, "fragment", "main");

        // 配置管线
        PipelineConfig config;
        config.vertShaderModule = vertShader.getModule();
        config.fragShaderModule = fragShader.getModule();
        config.vertEntry = vertShader.getEntryPointName().c_str();
        config.fragEntry = fragShader.getEntryPointName().c_str();

        // ⚠️ 改为三角形列表，并开启背面剔除 (由于没有深度缓冲，剔除背面是必须的！)
        config.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        config.cullMode = VK_CULL_MODE_BACK_BIT;
        config.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        // 顶点布局 (使用 Point3D_GPU，Stride 是 32)
        VkVertexInputBindingDescription bindingDesc{};
        bindingDesc.binding = 0;
        bindingDesc.stride = sizeof(Point3D_GPU); // 32 bytes
        bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        config.pVertexBindings = &bindingDesc;
        config.vertexBindingCount = 1;

        std::array<VkVertexInputAttributeDescription, 2> attrDesc{};
        // 属性0: 位置 (float3)
        attrDesc[0].binding = 0;
        attrDesc[0].location = 0;
        attrDesc[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrDesc[0].offset = offsetof(Point3D_GPU, x);  // 0
        // 属性1: 颜色 (float4)
        attrDesc[1].binding = 0;
        attrDesc[1].location = 1;
        attrDesc[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attrDesc[1].offset = offsetof(Point3D_GPU, r);  // 12

        config.pVertexAttributes = attrDesc.data();
        config.vertexAttributeCount = static_cast<uint32_t>(attrDesc.size());

        // ⚠️ 配置 Push Constants (推送常量)，用来传时间和宽高比
        VkPushConstantRange pcRange{};
        pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pcRange.offset = 0;
        pcRange.size = sizeof(float) * 2;
        config.pushConstantRanges.push_back(pcRange);

        Pipeline pipeline(vulkanInit.getDevice(), renderPass.get(), config);

        // 临时命令池用于数据上传
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = vulkanInit.getGraphicsFamily();
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        VkCommandPool uploadPool;
        vkCreateCommandPool(vulkanInit.getDevice(), &poolInfo, nullptr, &uploadPool);

        // 上传顶点缓冲
        auto vertexBuffer = Buffer::CreateAndUpload(
            vulkanInit.getDevice(), vulkanInit.getPhysicalDevice(), uploadPool,
            vulkanInit.getGraphicsQueue(), cubeData.points.data(),
            sizeof(Point3D_GPU) * cubeData.points.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
        );

        // ⚠️ 上传索引缓冲
        auto indexBuffer = Buffer::CreateAndUpload(
            vulkanInit.getDevice(), vulkanInit.getPhysicalDevice(), uploadPool,
            vulkanInit.getGraphicsQueue(), cubeData.indices.data(),
            sizeof(uint32_t) * cubeData.indices.size(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT
        );

        vkDestroyCommandPool(vulkanInit.getDevice(), uploadPool, nullptr);

        Presenter presenter(
            vulkanInit.getPhysicalDevice(), vulkanInit.getDevice(),
            vulkanInit.getSurface(), vulkanInit.getGraphicsFamily(),
            vulkanInit.getPresentFamily(), vulkanInit.getGraphicsQueue(),
            vulkanInit.getPresentQueue(), renderPass.get(), vulkanInit.getWindow()
        );

        // 记录启动时间，用于计算动画时间
        auto startTime = std::chrono::high_resolution_clock::now();

        bool running = true;
        SDL_Event event;
        while (running) {
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) running = false;
                if (event.type == SDL_EVENT_WINDOW_RESIZED) presenter.markResized();
            }

            uint32_t imageIndex;
            VkCommandBuffer cmd = presenter.beginFrame(imageIndex);
            if (cmd == VK_NULL_HANDLE) continue;

            VkRenderPassBeginInfo renderPassInfo{};
            renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            renderPassInfo.renderPass = renderPass.get();
            renderPassInfo.framebuffer = presenter.getFramebuffer(imageIndex);
            renderPassInfo.renderArea.offset = {0, 0};
            renderPassInfo.renderArea.extent = presenter.getExtent();

            VkClearValue clearColor = {{{0.05f, 0.05f, 0.05f, 1.0f}}};
            renderPassInfo.clearValueCount = 1;
            renderPassInfo.pClearValues = &clearColor;

            vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

            VkViewport viewport{};
            viewport.x = 0.0f; viewport.y = 0.0f;
            viewport.width = static_cast<float>(presenter.getExtent().width);
            viewport.height = static_cast<float>(presenter.getExtent().height);
            viewport.minDepth = 0.0f; viewport.maxDepth = 1.0f;
            vkCmdSetViewport(cmd, 0, 1, &viewport);

            VkRect2D scissor{};
            scissor.offset = {0, 0};
            scissor.extent = presenter.getExtent();
            vkCmdSetScissor(cmd, 0, 1, &scissor);

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.get());

            // ⚠️ 传递 Push Constants (计算当前时间和屏幕比例)
            auto currentTime = std::chrono::high_resolution_clock::now();
            float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();
            float aspect = viewport.width / viewport.height;
            float pcData[2] = { time, aspect };
            vkCmdPushConstants(cmd, pipeline.getLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pcData), pcData);

            // 绑定顶点和索引缓冲
            VkBuffer vertexBuffers[] = { vertexBuffer.getBuffer() };
            VkDeviceSize offsets[] = { 0 };
            vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
            vkCmdBindIndexBuffer(cmd, indexBuffer.getBuffer(), 0, VK_INDEX_TYPE_UINT32);

            // ⚠️ 使用 DrawIndexed 绘制三角形面
            vkCmdDrawIndexed(cmd, static_cast<uint32_t>(cubeData.indices.size()), 1, 0, 0, 0);

            vkCmdEndRenderPass(cmd);
            presenter.endFrame(cmd, imageIndex);
        }

        vkDeviceWaitIdle(vulkanInit.getDevice());

    } catch (const std::exception& e)
    {
        std::cerr << "\033[31m[Fatal Error]\033" << std::endl;
    }
}