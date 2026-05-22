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
#include <filesystem>
#include <array>

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
 */
inline ShaderCompileResult compileSlangToSPIRV(
    const std::string& sourceCode,
    const std::string& stage,
    const std::string& entryPoint = "",
    const std::vector<std::string>& includes = {}
) {
    auto* globalSession = SlangManager::instance().getGlobalSession();

    slang::TargetDesc targetDesc{};
    targetDesc.format = SLANG_SPIRV;
    targetDesc.profile = globalSession->findProfile("spirv_1_5");

    slang::SessionDesc sessionDesc{};
    sessionDesc.targets = &targetDesc;
    sessionDesc.targetCount = 1;

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

    Slang::ComPtr<slang::IEntryPoint> slangEntryPoint;
    if (!entryPoint.empty()) {
        module->findEntryPointByName(entryPoint.c_str(), slangEntryPoint.writeRef());
    } else {
        for (SlangInt i = 0; i < module->getDefinedEntryPointCount(); ++i) {
            module->getDefinedEntryPoint(i, slangEntryPoint.writeRef());
            if (slangEntryPoint) break;
        }
    }

    if (!slangEntryPoint) {
        throw std::runtime_error("Failed to find entry point");
    }

    std::array<slang::IComponentType*, 2> components = { module, slangEntryPoint.get() };
    Slang::ComPtr<slang::IComponentType> composedProgram;
    if (SLANG_FAILED(session->createCompositeComponentType(
            components.data(), components.size(),
            composedProgram.writeRef(),
            diagnosticsBlob.writeRef()))) {
        throw std::runtime_error("Failed to compose program");
    }

    Slang::ComPtr<slang::IComponentType> linkedProgram;
    if (SLANG_FAILED(composedProgram->link(
            linkedProgram.writeRef(),
            diagnosticsBlob.writeRef()))) {
        throw std::runtime_error("Failed to link program");
    }

    Slang::ComPtr<slang::IBlob> spirvBlob;
    if (SLANG_FAILED(linkedProgram->getEntryPointCode(
            0, 0,
            spirvBlob.writeRef(),
            diagnosticsBlob.writeRef()))) {
        throw std::runtime_error("Failed to get SPIR-V code");
    }

    // 核心修复：从已链接程序的 ProgramLayout 中安全提取 EntryPointReflection 派生的结构
    slang::ProgramLayout* programLayout = linkedProgram->getLayout();
    if (!programLayout) {
        throw std::runtime_error("Failed to get program layout from linked program");
    }
    slang::EntryPointLayout* entryPointLayout = programLayout->getEntryPointByIndex(0);
    if (!entryPointLayout) {
        throw std::runtime_error("Failed to get entry point layout from program layout");
    }

    const auto* spvData = static_cast<const uint8_t*>(spirvBlob->getBufferPointer());
    const size_t spvSize = spirvBlob->getBufferSize();

    ShaderCompileResult result;
    result.entryPointName = entryPointLayout->getName();
    result.spirv.assign(spvData, spvData + spvSize);
    result.vulkanStage =
        stage == "vertex"   ? VK_SHADER_STAGE_VERTEX_BIT :
        stage == "fragment" ? VK_SHADER_STAGE_FRAGMENT_BIT :
        stage == "compute"  ? VK_SHADER_STAGE_COMPUTE_BIT :
                              VK_SHADER_STAGE_VERTEX_BIT;

    return result;
}

/**
 * @brief 自动推导：编译 Slang 源文件，并自动提取和分类所有的 Entry Points (如 vertex, fragment, compute)
 */
inline std::vector<ShaderCompileResult> compileAllEntryPointsFromSlang(
    const std::string& sourceCode,
    const std::string& filename = "main.slang",
    const std::vector<std::string>& includes = {}
) {
    auto* globalSession = SlangManager::instance().getGlobalSession();

    slang::TargetDesc targetDesc{};
    targetDesc.format = SLANG_SPIRV;
    targetDesc.profile = globalSession->findProfile("spirv_1_5");

    slang::SessionDesc sessionDesc{};
    sessionDesc.targets = &targetDesc;
    sessionDesc.targetCount = 1;

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

    Slang::ComPtr<slang::IBlob> diagnosticsBlob;
    auto* module = session->loadModuleFromSourceString(
        "main", filename.c_str(),
        sourceCode.c_str(),
        diagnosticsBlob.writeRef()
    );
    if (!module) {
        std::string error(static_cast<const char*>(diagnosticsBlob->getBufferPointer()),
                         diagnosticsBlob->getBufferSize());
        throw std::runtime_error("Slang compilation error in " + filename + ": " + error);
    }

    std::vector<ShaderCompileResult> results;
    SlangInt entryPointCount = module->getDefinedEntryPointCount();

    for (SlangInt i = 0; i < entryPointCount; ++i) {
        Slang::ComPtr<slang::IEntryPoint> slangEntryPoint;
        module->getDefinedEntryPoint(i, slangEntryPoint.writeRef());
        if (!slangEntryPoint) continue;

        std::array<slang::IComponentType*, 2> components = { module, slangEntryPoint.get() };
        Slang::ComPtr<slang::IComponentType> composedProgram;
        if (SLANG_FAILED(session->createCompositeComponentType(
                components.data(), components.size(),
                composedProgram.writeRef(),
                diagnosticsBlob.writeRef()))) {
            continue;
        }

        Slang::ComPtr<slang::IComponentType> linkedProgram;
        if (SLANG_FAILED(composedProgram->link(linkedProgram.writeRef(), diagnosticsBlob.writeRef()))) {
            continue;
        }

        Slang::ComPtr<slang::IBlob> spirvBlob;
        if (SLANG_FAILED(linkedProgram->getEntryPointCode(0, 0, spirvBlob.writeRef(), diagnosticsBlob.writeRef()))) {
            continue;
        }

        // 核心修复：从 linkedProgram 中安全获取 EntryPointReflection
        slang::ProgramLayout* programLayout = linkedProgram->getLayout();
        if (!programLayout) continue;

        slang::EntryPointLayout* entryPointLayout = programLayout->getEntryPointByIndex(0);
        if (!entryPointLayout) continue;

        SlangStage stage = entryPointLayout->getStage();

        VkShaderStageFlagBits vulkanStage = VK_SHADER_STAGE_VERTEX_BIT;
        switch (stage) {
            case SLANG_STAGE_VERTEX:
                vulkanStage = VK_SHADER_STAGE_VERTEX_BIT;
                break;
            case SLANG_STAGE_FRAGMENT:
                vulkanStage = VK_SHADER_STAGE_FRAGMENT_BIT;
                break;
            case SLANG_STAGE_COMPUTE:
                vulkanStage = VK_SHADER_STAGE_COMPUTE_BIT;
                break;
            default:
                continue; // 过滤非核心阶段以保证安全
        }

        const auto* spvData = static_cast<const uint8_t*>(spirvBlob->getBufferPointer());
        const size_t spvSize = spirvBlob->getBufferSize();

        ShaderCompileResult result;
        result.entryPointName = entryPointLayout->getName();
        result.spirv.assign(spvData, spvData + spvSize);
        result.vulkanStage = vulkanStage;
        results.push_back(result);
    }

    return results;
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
 * @brief 封装可被 Vulkan 管线直接使用的着色器模块 (RAII)
 */
class ShaderModule {
public:
    ShaderModule() : device_(VK_NULL_HANDLE), module_(VK_NULL_HANDLE) {}

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

    static ShaderModule fromSPIRV(VkDevice device, const std::vector<uint8_t>& spirv, const std::string& entryPoint, VkShaderStageFlagBits stage) {
        ShaderCompileResult result;
        result.spirv = spirv;
        result.entryPointName = entryPoint;
        result.vulkanStage = stage;
        return ShaderModule(device, result);
    }

    ~ShaderModule() {
        cleanup();
    }

    ShaderModule(const ShaderModule&) = delete;
    ShaderModule& operator=(const ShaderModule&) = delete;

    ShaderModule(ShaderModule&& other) noexcept
        : device_(other.device_), module_(other.module_),
          entryPointName_(std::move(other.entryPointName_)),
          vulkanStage_(other.vulkanStage_)
    {
        other.module_ = VK_NULL_HANDLE;
        other.device_ = VK_NULL_HANDLE;
    }

    ShaderModule& operator=(ShaderModule&& other) noexcept {
        if (this != &other) {
            cleanup();
            device_ = other.device_;
            module_ = other.module_;
            entryPointName_ = std::move(other.entryPointName_);
            vulkanStage_ = other.vulkanStage_;
            other.module_ = VK_NULL_HANDLE;
            other.device_ = VK_NULL_HANDLE;
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

    void cleanup() {
        if (device_ != VK_NULL_HANDLE && module_ != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device_, module_, nullptr);
            module_ = VK_NULL_HANDLE;
        }
        device_ = VK_NULL_HANDLE;
    }

    VkDevice              device_ = VK_NULL_HANDLE;
    VkShaderModule        module_ = VK_NULL_HANDLE;
    std::string           entryPointName_;
    VkShaderStageFlagBits vulkanStage_ = VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;

    friend class ShaderLibrary;
};

/**
 * @brief 自动化着色器库管理器
 * 支持一键扫描、多入口编译与灵活检索获取
 */
class ShaderLibrary {
public:
    ShaderLibrary(VkDevice device) : device_(device) {}
    ~ShaderLibrary() = default;

    ShaderLibrary(const ShaderLibrary&) = delete;
    ShaderLibrary& operator=(const ShaderLibrary&) = delete;

    ShaderLibrary(ShaderLibrary&&) noexcept = default;
    ShaderLibrary& operator=(ShaderLibrary&&) noexcept = default;

    /**
     * @brief 自动扫描并加载指定目录下的所有 Slang 着色器源码文件
     */
    void loadDirectory(const std::string& directoryPath, const std::vector<std::string>& includes = {}) {
        namespace fs = std::filesystem;
        if (!fs::exists(directoryPath) || !fs::is_directory(directoryPath)) {
            throw std::runtime_error("ShaderLibrary: Directory path is invalid or does not exist: " + directoryPath);
        }

        for (const auto& entry : fs::directory_iterator(directoryPath)) {
            if (entry.is_regular_file() && entry.path().extension() == ".slang") {
                std::string filepath = entry.path().string();
                std::string filename = entry.path().filename().string();
                std::string stemName = entry.path().stem().string();

                std::ifstream file(filepath, std::ios::binary);
                if (!file.is_open()) continue;
                std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                file.close();

                // 编译源文件，自动识别并生成所有合法的 entry points 着色器模块
                auto compileResults = compileAllEntryPointsFromSlang(source, filename, includes);
                for (const auto& result : compileResults) {
                    ShaderModule module(device_, result);

                    // 构造混合索引键名，例如 points.slang:vertexMain 以及 points:vertexMain
                    std::string fullKey = filename + ":" + result.entryPointName;
                    std::string stemKey = stemName + ":" + result.entryPointName;

                    library_[fullKey] = std::move(module);

                    if (library_.find(stemKey) == library_.end()) {
                        ShaderModule dupModule = ShaderModule::fromSPIRV(device_, result.spirv, result.entryPointName, result.vulkanStage);
                        library_[stemKey] = std::move(dupModule);
                    }
                }
            }
        }
    }

    /**
     * @brief 根据文件名和入口名直接检索获取着色器模块
     * @param filename 可以是 points.slang 或 缩写 points
     * @param entryPoint 例如 vertexMain
     */
    const ShaderModule& get(const std::string& filename, const std::string& entryPoint) const {
        std::string key = filename + ":" + entryPoint;
        auto it = library_.find(key);
        if (it != library_.end()) {
            return it->second;
        }
        throw std::runtime_error("ShaderLibrary: Failed to locate shader module under key: " + key);
    }

    bool has(const std::string& filename, const std::string& entryPoint) const {
        std::string key = filename + ":" + entryPoint;
        return library_.find(key) != library_.end();
    }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    std::unordered_map<std::string, ShaderModule> library_;
};

} // namespace StuCanvas::Vulkan