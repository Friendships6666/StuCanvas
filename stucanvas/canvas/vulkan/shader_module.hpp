// stucanvas/canvas/vulkan/shader_module.hpp

#pragma once

#include <vulkan/vulkan.h>
#include <slang.h>
#include <slang-com-ptr.h>
#include <slang-com-helper.h>

#include <string>
#include <vector>
#include <memory>
#include <stdexcept>
#include <fstream>
#include <optional>
#include <unordered_map>

namespace StuCanvas::Vulkan {

/**
 * @brief Slang 编译管理器（单例模式，全局共享一个会话以利用缓存）
 */
class SlangManager {
public:
    static SlangManager& instance() {
        static SlangManager manager;
        return manager;
    }

    slang::IGlobalSession* getGlobalSession() const { return globalSession_; }

    SlangManager(const SlangManager&) = delete;
    SlangManager& operator=(const SlangManager&) = delete;

private:
    SlangManager() {
        if (SLANG_FAILED(slang::createGlobalSession(globalSession_.writeRef()))) {
            throw std::runtime_error("Failed to create Slang global session");
        }
    }

    Slang::ComPtr<slang::IGlobalSession> globalSession_;
};

/**
 * @brief 着色器编译结果：包含 SPIR‑V 字节码及入口点信息
 */
struct ShaderCompileResult {
    std::string entryPointName;           // 着色器入口函数名
    VkShaderStageFlagBits vulkanStage;    // Vulkan 着色器阶段
    std::vector<uint8_t> spirv;           // 编译后的 SPIR‑V 字节码
};

/**
 * @brief 通过 Slang 编译器将源代码编译为 SPIR‑V
 *
 * @param sourceCode Slang 源码字符串
 * @param stage      目标着色器阶段（如 "vertex"、"fragment"、"compute"）
 * @param entryPoint 入口函数名，若为空则自动匹配
 * @param includes   可选的 #include 搜索路径列表
 * @return ShaderCompileResult 编译结果
 */
inline ShaderCompileResult compileSlangToSPIRV(
    const std::string& sourceCode,
    const std::string& stage,
    const std::string& entryPoint = "",
    const std::vector<std::string>& includes = {}
) {
    auto* globalSession = SlangManager::instance().getGlobalSession();

    // 1. 配置编译目标为 SPIR‑V
    slang::TargetDesc targetDesc{};
    targetDesc.format = SLANG_SPIRV;
    targetDesc.profile = globalSession->findProfile("spirv_1_5");

    // 2. 创建编译会话
    slang::SessionDesc sessionDesc{};
    sessionDesc.targets = &targetDesc;
    sessionDesc.targetCount = 1;

    // 设置包含路径
    std::vector<const char*> includePaths;
    for (const auto& inc : includes) {
        includePaths.push_back(inc.c_str());
    }
    sessionDesc.searchPaths = includePaths.data();
    sessionDesc.searchPathCount = static_cast<SlangInt>(includePaths.size());

    Slang::ComPtr<slang::ISession> session;
    if (SLANG_FAILED(globalSession->createSession(sessionDesc, session.writeRef()))) {
        throw std::runtime_error("Failed to create Slang session");
    }

    // 3. 加载模块
    Slang::ComPtr<slang::IBlob> diagnosticsBlob;
    auto* module = session->loadModuleFromSourceString(
        "main", "main.slang",
        sourceCode.c_str(),
        diagnosticsBlob.writeRef()
    );
    if (!module) {
        std::string error(static_cast<const char*>(diagnosticsBlob->getBufferPointer()),
                         diagnosticsBlob->getBufferSize());
        throw std::runtime_error("Slang compilation error: " + error);
    }

    // 4. 查找入口点
    Slang::ComPtr<slang::IEntryPoint> slangEntryPoint;
    if (!entryPoint.empty()) {
        module->findEntryPointByName(entryPoint.c_str(), slangEntryPoint.writeRef());
    } else {
        // 自动选择第一个定义的入口点（不按阶段过滤）
        for (SlangInt i = 0; i < module->getDefinedEntryPointCount(); ++i) {
            module->getDefinedEntryPoint(i, slangEntryPoint.writeRef());
            if (slangEntryPoint) break;
        }
    }

    if (!slangEntryPoint) {
        throw std::runtime_error("Failed to find entry point");
    }

    if (!slangEntryPoint) {
        throw std::runtime_error("Failed to find entry point");
    }

    // 5. 合成程序组件
    std::array<slang::IComponentType*, 2> components = { module, slangEntryPoint.get() };
    Slang::ComPtr<slang::IComponentType> composedProgram;
    if (SLANG_FAILED(session->createCompositeComponentType(
            components.data(), components.size(),
            composedProgram.writeRef(),
            diagnosticsBlob.writeRef()))) {
        throw std::runtime_error("Failed to compose program");
    }

    // 6. 链接
    Slang::ComPtr<slang::IComponentType> linkedProgram;
    if (SLANG_FAILED(composedProgram->link(
            linkedProgram.writeRef(),
            diagnosticsBlob.writeRef()))) {
        throw std::runtime_error("Failed to link program");
    }

    // 7. 获取 SPIR‑V 代码
    Slang::ComPtr<slang::IBlob> spirvBlob;
    if (SLANG_FAILED(linkedProgram->getEntryPointCode(
            0, 0,
            spirvBlob.writeRef(),
            diagnosticsBlob.writeRef()))) {
        throw std::runtime_error("Failed to get SPIR-V code");
    }

    // 8. 构建返回结果
    const auto* spvData = static_cast<const uint8_t*>(spirvBlob->getBufferPointer());
    const size_t spvSize = spirvBlob->getBufferSize();

    ShaderCompileResult result;
    result.entryPointName = entryPoint.empty() ? slangEntryPoint->getFunctionReflection()->getName() : entryPoint;
    result.spirv.assign(spvData, spvData + spvSize);
    result.vulkanStage =
        stage == "vertex"   ? VK_SHADER_STAGE_VERTEX_BIT :
        stage == "fragment" ? VK_SHADER_STAGE_FRAGMENT_BIT :
        stage == "compute"  ? VK_SHADER_STAGE_COMPUTE_BIT :
                              VK_SHADER_STAGE_VERTEX_BIT;

    result.entryPointName = entryPoint.empty()
                        ? slangEntryPoint->getFunctionReflection()->getName()
                        : entryPoint;

    return result;
}

/**
 * @brief 从文件读取源码并编译
 */
inline ShaderCompileResult compileSlangFile(
    const std::string& filepath,
    const std::string& stage,
    const std::string& entryPoint = "",
    const std::vector<std::string>& includes = {}
) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open shader file: " + filepath);
    }

    std::string source(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>()
    );

    return compileSlangToSPIRV(source, stage, entryPoint, includes);
}

/**
 * @brief 封装可被 Vulkan 管线直接使用的着色器模块
 */
class ShaderModule {
public:
    ShaderModule() : device_(VK_NULL_HANDLE), module_(VK_NULL_HANDLE) {}

    /**
     * @brief 从 Slang 源码字符串编译并创建着色器模块
     */
    static ShaderModule fromSlangSource(
        VkDevice device,
        const std::string& sourceCode,
        const std::string& stage,
        const std::string& entryPoint = "",
        const std::vector<std::string>& includes = {}
    ) {
        auto compiled = compileSlangToSPIRV(sourceCode, stage, entryPoint, includes);
        return ShaderModule(device, compiled);
    }

    /**
     * @brief 从 Slang 源码文件编译并创建着色器模块
     */
    static ShaderModule fromSlangFile(
        VkDevice device,
        const std::string& filepath,
        const std::string& stage,
        const std::string& entryPoint = "",
        const std::vector<std::string>& includes = {}
    ) {
        auto compiled = compileSlangFile(filepath, stage, entryPoint, includes);
        return ShaderModule(device, compiled);
    }

    /**
     * @brief 从预编译的 SPIR‑V 字节码创建着色器模块
     */
    static ShaderModule fromSPIRV(VkDevice device, const std::vector<uint8_t>& spirv) {
        ShaderCompileResult result;
        result.spirv = spirv;
        result.entryPointName = "main";
        result.vulkanStage = VK_SHADER_STAGE_VERTEX_BIT;
        return ShaderModule(device, result);
    }

    ~ShaderModule() {
        if (module_ != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device_, module_, nullptr);
        }
    }

    ShaderModule(const ShaderModule&) = delete;
    ShaderModule& operator=(const ShaderModule&) = delete;

    ShaderModule(ShaderModule&& other) noexcept
        : device_(other.device_), module_(other.module_),
          entryPointName_(std::move(other.entryPointName_)),
          vulkanStage_(other.vulkanStage_)
    {
        other.module_ = VK_NULL_HANDLE;
    }

    ShaderModule& operator=(ShaderModule&& other) noexcept {
        if (this != &other) {
            if (module_ != VK_NULL_HANDLE) vkDestroyShaderModule(device_, module_, nullptr);
            device_ = other.device_;
            module_ = other.module_;
            entryPointName_ = std::move(other.entryPointName_);
            vulkanStage_ = other.vulkanStage_;
            other.module_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    VkShaderModule       getModule() const { return module_; }
    const std::string&   getEntryPointName() const { return entryPointName_; }
    VkShaderStageFlagBits getVulkanStage()  const { return vulkanStage_; }

private:
    ShaderModule(VkDevice device, const ShaderCompileResult& compileResult)
        : device_(device),
          entryPointName_(compileResult.entryPointName),
          vulkanStage_(compileResult.vulkanStage)
    {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = compileResult.spirv.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(compileResult.spirv.data());

        if (vkCreateShaderModule(device_, &createInfo, nullptr, &module_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create shader module");
        }
    }

    VkDevice              device_ = VK_NULL_HANDLE;
    VkShaderModule        module_ = VK_NULL_HANDLE;
    std::string           entryPointName_;
    VkShaderStageFlagBits vulkanStage_ = VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
};

} // namespace StuCanvas::Vulkan