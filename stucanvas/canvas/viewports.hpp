/***************************************************************************
 * Copyright (c) 2025-2026 Tian Yuxuan (Friendships666)                    *
 *                                                                          *
 * StuCanvas is licensed under Mulan PSL v2.                                *
 * You can use this software according to the terms and conditions of the   *
 * Mulan PSL v2.                                                            *
 * You may obtain a copy of Mulan PSL v2 at:                                *
 *          http://license.coscl.org.cn/MulanPSL2                           *
 *                                                                          *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF     *
 * ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO        *
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.      *
 * See the Mulan PSL v2 for more details.                                   *
 ***************************************************************************/

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <eigen3/Eigen/Dense>

namespace StuCanvas
{


    enum class BlendMode
    {
        None,                 ///< 无混合（直接覆盖）：C_dest = C_src
        AlphaBlend,           ///< 标准 Alpha 混合（直观半透明）：C_dest = C_src * A_src + C_dest * (1 - A_src)
        PremultipliedAlpha,   ///< 预乘 Alpha 混合（视频编辑/NLE 行业标准，完全杜绝黑边半透明）
        Additive,             ///< 加性混合（用于光晕、极光特效）：C_dest = C_src + C_dest
        Multiply              ///< 乘性混合（正片叠底，用于阴影、滤镜）：C_dest = C_src * C_dest

    };
    struct ViewPort
    {
        float x;        ///< 归一化相对横坐标 [-1.0f, 1.0f]
        float y;        ///< 归一化相对纵坐标 [-1.0f, 1.0f]
        float width;    ///< 归一化相对宽度 [0.0f, 1.0f]
        float height;   ///< 归一化相对高度 [0.0f, 1.0f]
        void* appearance;
    };

}   // namespace StuCanvas
