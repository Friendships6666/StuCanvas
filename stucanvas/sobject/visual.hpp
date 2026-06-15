/***************************************************************************
* Copyright (c) 2026 Tian Yuxuan (Friendships666)                          *
*                                                                          *
* Distributed under the terms of the MIT License.                          *
*                                                                          *
* The full license is in the file LICENSE, distributed with this software. *
***************************************************************************/

#pragma once
#include <eigen3/Eigen/Dense>
#include <memory>
#include <algorithm>
#include <cstdint>
#include "../utils/tiny_vector.hpp" // 引入底层对齐分配器助手

namespace StuCanvas
{
    // ─────────────────────────────────────────────────────────────────────────
    // 1. 材质渲染路径枚举
    // ─────────────────────────────────────────────────────────────────────────
    enum class VisualPath : uint8_t
    {
        CAD_LIGHTWEIGHT, // 基础 CAD 极速绘图路径 (SObject 标准)
        HIGH_END_PBR,     // 高端物理拟真渲染路径 (OpenPBR 标准)
        HYBRID           // 混合双路径
    };

    // ─────────────────────────────────────────────────────────────────────────
    // 2. SObject 基础绘图配置结构体（40 字节）
    // ─────────────────────────────────────────────────────────────────────────
    struct SObjectStyle
    {
        Eigen::Matrix<float, 4, 1> stroke_color = Eigen::Matrix<float, 4, 1>(0.0f, 0.0f, 0.0f, 1.0f);
        Eigen::Matrix<float, 4, 1> fill_color   = Eigen::Matrix<float, 4, 1>(0.8f, 0.8f, 0.8f, 1.0f);

        float stroke_width = 1.0f;  // CAD 绘图线宽

        uint16_t is_visible     : 1 = 1;     // 是否可见
        uint16_t show_wireframe : 1 = 1;     // 是否显示网格/线框
        uint16_t show_vertices  : 1 = 0;     // 是否显示端点/控制点
        uint16_t is_transparent : 1 = 0;     // 是否透明
        uint16_t padding        : 12 = 0;    // 位域填充

        uint16_t align_pad      = 0;         // 保持 4 字节边界对齐
    };

    // ─────────────────────────────────────────────────────────────────────────
    // 3. OpenPBR ASWF/MaterialX 工业级物理材质配置（堆资产）
    // ─────────────────────────────────────────────────────────────────────────
    struct OpenPBRConfig
    {
        // --- A. Base Layer (基础层：金属或介质衬底) ---
        float base_weight = 1.0f;
        Eigen::Matrix<float, 3, 1> base_color = Eigen::Matrix<float, 3, 1>::Ones();
        float base_metalness = 0.0f;
        float base_diffuse_roughness = 0.0f;

        // --- B. Specular Lobe (高光物理反射层) ---
        float specular_weight = 1.0f;
        Eigen::Matrix<float, 3, 1> specular_color = Eigen::Matrix<float, 3, 1>::Ones();
        float specular_roughness = 0.3f;
        float specular_ior = 1.5f;
        float specular_anisotropy = 0.0f;

        // --- C. Subsurface Lobe (次表面散射层) ---
        float subsurface_weight = 0.0f;
        Eigen::Matrix<float, 3, 1> subsurface_color = Eigen::Matrix<float, 3, 1>::Ones();
        float subsurface_radius = 1.0f;
        Eigen::Matrix<float, 3, 1> subsurface_radius_scale = Eigen::Matrix<float, 3, 1>::Ones();

        // --- D. Transmission Lobe (物理透射层) ---
        float transmission_weight = 0.0f;
        Eigen::Matrix<float, 3, 1> transmission_color = Eigen::Matrix<float, 3, 1>::Ones();
        float transmission_depth = 1.0f;

        // --- E. Coat Layer (清漆覆膜层) ---
        float coat_weight = 0.0f;
        Eigen::Matrix<float, 3, 1> coat_color = Eigen::Matrix<float, 3, 1>::Ones();
        float coat_roughness = 0.1f;
        float coat_ior = 1.5f;

        // --- F. Sheen Lobe (茸毛层) ---
        float sheen_weight = 0.0f;
        Eigen::Matrix<float, 3, 1> sheen_color = Eigen::Matrix<float, 3, 1>::Ones();
        float sheen_roughness = 0.3f;

        // --- G. Emission Lobe (物理自发光层) ---
        float emission_weight = 0.0f;
        Eigen::Matrix<float, 3, 1> emission_color = Eigen::Matrix<float, 3, 1>::Zero();

        // --- H. Thin Film (薄膜干涉层) ---
        float thin_film_weight = 0.0f;
        float thin_film_thickness = 0.5f;

        // --- I. Geometry & Thin Walled (几何与薄壁设置) ---
        uint32_t geometry_thin_walled : 1 = 0;
    };

    // ─────────────────────────────────────────────────────────────────────────
    // 4. VisualConfig 享元控制类（强制堆分配版，无任何编译警告）
    // ─────────────────────────────────────────────────────────────────────────
    class VisualConfig
    {
    private:
        // 🚀 核心限制 1：将构造与析构私有化，彻底斩断栈分配的物理通道
        VisualConfig() noexcept = default;
        ~VisualConfig() noexcept = default;

        // 🚀 核心限制 2：删除所有拷贝、移动构造与赋值，防止在栈上产生任何中途拷贝复制
        VisualConfig(const VisualConfig&) = delete;
        VisualConfig& operator=(const VisualConfig&) = delete;
        VisualConfig(VisualConfig&&) = delete;
        VisualConfig& operator=(VisualConfig&&) = delete;

    public:
        // 极致对齐：锁定为 16 字节
        static constexpr size_t Alignment = 16;

        // 🚀 核心暴露：公有属性（Data-Oriented 设计，方便渲染管线直接提取）
        uint32_t id = 0;
        VisualPath rendering_path = VisualPath::CAD_LIGHTWEIGHT;

        // CAD 基础样式直接内联（40 字节）
        SObjectStyle cad_style;

        // OpenPBR 工业配置指针（8 字节）
        std::unique_ptr<OpenPBRConfig> pbr_spec = nullptr;

        // ─────────────────────────────────────────────────────────────────────
        // 5. 唯一合法的堆构建与销毁接口（Static Factory）
        // ─────────────────────────────────────────────────────────────────────

        /**
         * @brief 在堆上分配并就地构造一个 VisualConfig 对象（唯一的创建方式）
         */
        [[nodiscard]] static VisualConfig* Create()
        {
            // 通过自适应分配器在堆上分配物理空间，并增加 NOLINT 抑制分析器误报
            void* raw = ::StuCanvas::utils::detail::aligned_alloc_helper(sizeof(VisualConfig), Alignment); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
            return new (raw) VisualConfig();
        }

        /**
         * @brief 手动释放并销毁一个堆上的 VisualConfig 对象（唯一的销毁方式）
         */
        static void Destroy(VisualConfig* ptr) noexcept
        {
            if (!ptr) return;
            ptr->~VisualConfig(); // 手动唤醒析构（安全释放 std::unique_ptr）
            ::StuCanvas::utils::detail::aligned_free_helper(ptr, Alignment);
        }

        /**
         * @brief 显式克隆方法（生成一个全新的堆对象）
         */
        [[nodiscard]] VisualConfig* Clone() const
        {
            VisualConfig* clone = Create();
            clone->id = id;
            clone->rendering_path = rendering_path;
            clone->cad_style = cad_style;
            if (pbr_spec) {
                clone->pbr_spec = std::make_unique<OpenPBRConfig>(*pbr_spec);
            }
            return clone;
        }

        // 激活并延迟初始化 OpenPBR 标准配置
        void EnableOpenPBR()
        {
            if (!pbr_spec) {
                pbr_spec = std::make_unique<OpenPBRConfig>();
            }
            rendering_path = VisualPath::HIGH_END_PBR;
        }

        // 判定当前是否具备物理 PBR 材质数据
        [[nodiscard]] bool HasPBR() const noexcept
        {
            return pbr_spec != nullptr;
        }
    };
} // namespace StuCanvas