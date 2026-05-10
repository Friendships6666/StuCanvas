// main.cpp (使用 Eigen 构建 MVP)
#include <iostream>
#include <vector>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>

#include <Eigen/Dense>          // 新增

#include "stucanvas/canvas/vulkan/init.hpp"
#include "stucanvas/canvas/vulkan/swap_chains.hpp"
#include "stucanvas/canvas/vulkan/renderpass.hpp"
#include "stucanvas/canvas/vulkan/pipeline.hpp"
#include "stucanvas/canvas/vulkan/shader_module.hpp"
#include "stucanvas/canvas/vulkan/buffer.hpp"
#include "stucanvas/canvas/vulkan/present.hpp"

#include "stucanvas/types/point.hpp"

using namespace StuCanvas::Vulkan;
using namespace StuCanvas;

// 线段数据结构（与 Slang 中的 Segment 严格对齐 std430）
struct Segment {
    float startPos[3];
    float _pad0;        // 使 startPos 占 16 字节
    float endPos[3];
    float _pad1;
    float color[4];
};

// Push constant 对应着色器中的 Globals
struct PushConstants {
    float mvp[16];          // 列主序 4x4 矩阵
    float viewportSize[2];
    float lineWidth;
};

// 生成螺旋线段
std::vector<Segment> generateHelix(float radius, float height, int turns, int segments) {
    std::vector<Segment> segs;
    segs.reserve(segments);
    for (int i = 0; i < segments; ++i) {
        float t1 = float(i) / float(segments);
        float t2 = float(i + 1) / float(segments);
        float angle1 = float(turns) * 2.0f * 3.14159265f * t1;
        float angle2 = float(turns) * 2.0f * 3.14159265f * t2;

        Segment seg;
        seg.startPos[0] = radius * cos(angle1);
        seg.startPos[1] = radius * sin(angle1);
        seg.startPos[2] = -height + 2.0f * height * t1;
        seg.endPos[0]   = radius * std::cos(angle2);
        seg.endPos[1]   = radius * sin(angle2);
        seg.endPos[2]   = -height + 2.0f * height * t2;

        seg.color[0] = 1.0f - t1;
        seg.color[1] = 0.3f;
        seg.color[2] = t1;
        seg.color[3] = 1.0f;

        segs.push_back(seg);
    }
    return segs;
}

// 使用 Eigen 计算列主序 MVP 矩阵（Vulkan 坐标系）
void computeMVP(float time, float aspect, float mvp[16]) {
    using namespace Eigen;

    // 透视投影 (Vulkan: Y down, depth [0,1])
    float fov = 60.0f * M_PI / 180.0f;
    float near = 0.1f, far = 100.0f;
    float f = 1.0f / tan(fov * 0.5f);
    Matrix4f proj = Matrix4f::Zero();
    proj(0,0) = f / aspect;
    proj(1,1) = -f;
    proj(2,2) = far / (far - near);
    proj(3,2) = -far * near / (far - near);
    proj(2,3) = 1.0f;

    // 摄像机绕 Y 轴旋转，看向原点
    float camDist = 3.0f;
    float angle = time * 0.5f;
    Vector3f eye(camDist * sin(angle), 0.5f, camDist * cos(angle));
    Vector3f center(0.0f, 0.0f, 0.0f);
    Vector3f up(0.0f, -1.0f, 0.0f);   // Vulkan Y down

    Vector3f f_dir = (center - eye).normalized();
    Vector3f s = f_dir.cross(up).normalized();
    Vector3f u = s.cross(f_dir);

    Matrix4f view = Matrix4f::Identity();
    view(0,0) = s.x(); view(0,1) = s.y(); view(0,2) = s.z(); view(0,3) = -s.dot(eye);
    view(1,0) = u.x(); view(1,1) = u.y(); view(1,2) = u.z(); view(1,3) = -u.dot(eye);
    view(2,0) = -f_dir.x(); view(2,1) = -f_dir.y(); view(2,2) = -f_dir.z(); view(2,3) = f_dir.dot(eye);

    Matrix4f mvp_mat = proj * view;

    // 列主序输出
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            mvp[col * 4 + row] = mvp_mat(row, col);
}

int main() {
    try {
        VulkanInit vkInit("StuCanvas SDF Line Strip", 800, 600, true);

        VkFormat swapchainFormat;
        {
            SwapChain tempSC(
                vkInit.getPhysicalDevice(), vkInit.getDevice(),
                vkInit.getSurface(), VK_NULL_HANDLE,
                vkInit.getGraphicsFamily(), vkInit.getPresentFamily(),
                vkInit.getWindow()
            );
            swapchainFormat = tempSC.getImageFormat();
        }

        RenderPass renderPass(vkInit.getDevice(), swapchainFormat);

        // 编译着色器 (入口点设为 main，Slang 会自动映射)
        auto vertModule = ShaderModule::fromSlangFile(
            vkInit.getDevice(), "/home/friendships666/Projects/StuCanvas/stucanvas/shaders/segments.slang", "vertex", "vertexMain");
        auto fragModule = ShaderModule::fromSlangFile(
            vkInit.getDevice(), "/home/friendships666/Projects/StuCanvas/stucanvas/shaders/segments.slang", "fragment", "fragmentMain");

        // 描述符集布局
        VkDescriptorSetLayoutBinding layoutBinding{};
        layoutBinding.binding = 0;
        layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        layoutBinding.descriptorCount = 1;
        layoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &layoutBinding;

        VkDescriptorSetLayout descSetLayout;
        vkCreateDescriptorSetLayout(vkInit.getDevice(), &layoutInfo, nullptr, &descSetLayout);

        // 管线配置
        PipelineConfig config;
        config.vertShaderModule = vertModule.getModule();
        config.fragShaderModule = fragModule.getModule();
        config.vertEntry = "main";
        config.fragEntry = "main";

        config.vertexBindingCount = 0;
        config.pVertexBindings = nullptr;
        config.vertexAttributeCount = 0;
        config.pVertexAttributes = nullptr;

        config.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        config.cullMode = VK_CULL_MODE_NONE;
        config.frontFace = VK_FRONT_FACE_CLOCKWISE;

        config.descriptorSetLayouts.push_back(descSetLayout);

        VkPushConstantRange pcRange{};
        pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pcRange.offset = 0;
        pcRange.size = sizeof(PushConstants);
        config.pushConstantRanges.push_back(pcRange);

        Pipeline pipeline(vkInit.getDevice(), renderPass.get(), config);

        // 线段数据
        auto segments = generateHelix(0.6f, 0.8f, 5, 200);
        size_t segmentsSize = segments.size() * sizeof(Segment);

        VkCommandPool uploadPool;
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = vkInit.getGraphicsFamily();
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        vkCreateCommandPool(vkInit.getDevice(), &poolInfo, nullptr, &uploadPool);

        auto segmentBuffer = Buffer::CreateAndUpload(
            vkInit.getDevice(), vkInit.getPhysicalDevice(), uploadPool,
            vkInit.getGraphicsQueue(), segments.data(), segmentsSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
        );

        // 描述符集
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        poolSize.descriptorCount = 1;

        VkDescriptorPoolCreateInfo descPoolInfo{};
        descPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descPoolInfo.poolSizeCount = 1;
        descPoolInfo.pPoolSizes = &poolSize;
        descPoolInfo.maxSets = 1;

        VkDescriptorPool descPool;
        vkCreateDescriptorPool(vkInit.getDevice(), &descPoolInfo, nullptr, &descPool);

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descSetLayout;

        VkDescriptorSet descSet;
        vkAllocateDescriptorSets(vkInit.getDevice(), &allocInfo, &descSet);

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = segmentBuffer.getBuffer();
        bufferInfo.offset = 0;
        bufferInfo.range = segmentsSize;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descSet;
        write.dstBinding = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(vkInit.getDevice(), 1, &write, 0, nullptr);
        vkDestroyCommandPool(vkInit.getDevice(), uploadPool, nullptr);

        // 呈现器
        Presenter presenter(
            vkInit.getPhysicalDevice(), vkInit.getDevice(),
            vkInit.getSurface(), vkInit.getGraphicsFamily(),
            vkInit.getPresentFamily(), vkInit.getGraphicsQueue(),
            vkInit.getPresentQueue(), renderPass.get(), vkInit.getWindow()
        );

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

            VkRenderPassBeginInfo rpBegin{};
            rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rpBegin.renderPass = renderPass.get();
            rpBegin.framebuffer = presenter.getFramebuffer(imageIndex);
            rpBegin.renderArea.offset = {0, 0};
            rpBegin.renderArea.extent = presenter.getExtent();
            VkClearValue clearColor = {{{0.02f, 0.02f, 0.05f, 1.0f}}};
            rpBegin.clearValueCount = 1;
            rpBegin.pClearValues = &clearColor;

            vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

            VkViewport viewport{};
            viewport.width = static_cast<float>(presenter.getExtent().width);
            viewport.height = static_cast<float>(presenter.getExtent().height);
            viewport.minDepth = 0.0f; viewport.maxDepth = 1.0f;
            vkCmdSetViewport(cmd, 0, 1, &viewport);

            VkRect2D scissor{};
            scissor.offset = {0, 0};
            scissor.extent = presenter.getExtent();
            vkCmdSetScissor(cmd, 0, 1, &scissor);

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.get());

            auto now = std::chrono::high_resolution_clock::now();
            float time = std::chrono::duration<float>(now - startTime).count();
            float aspect = viewport.width / viewport.height;

            PushConstants pc;
            computeMVP(time, aspect, pc.mvp);
            pc.viewportSize[0] = viewport.width;
            pc.viewportSize[1] = viewport.height;
            pc.lineWidth = 2.0f;

            vkCmdPushConstants(cmd, pipeline.getLayout(),
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(PushConstants), &pc);

            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipeline.getLayout(), 0, 1, &descSet, 0, nullptr);

            vkCmdDraw(cmd, 6, static_cast<uint32_t>(segments.size()), 0, 0);

            vkCmdEndRenderPass(cmd);
            presenter.endFrame(cmd, imageIndex);
        }

        vkDeviceWaitIdle(vkInit.getDevice());
        vkDestroyDescriptorSetLayout(vkInit.getDevice(), descSetLayout, nullptr);
        vkDestroyDescriptorPool(vkInit.getDevice(), descPool, nullptr);
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}