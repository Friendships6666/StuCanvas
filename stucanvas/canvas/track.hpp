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
#include <functional>
#include <stdexcept>
#include <vector>

#include "../objects/dag/graph.hpp"
#include "cameras.hpp"
#include "pinned_vector.hpp"
#include "tiny_vector.hpp"
namespace StuCanvas
{
    struct Clip;

    // 🚀 定义可变回调函数指针
    using UpdateFunc = std::function< void ( Clip&, uint64_t start_frame, double start_ms ) >;

    /**
     * @brief 纯时间轴 NLE 剪辑实体 (不掺杂任何渲染或图结构)
     */
    struct Clip
    {
        uint64_t start_frame{};   ///< 起始绝对帧
        uint64_t end_frame{};     ///< 结束绝对帧（含）
        double start_ms = 0.0;    ///< 起始绝对毫秒
        double end_ms = 0.0;      ///< 结束绝对毫秒（含）

        UpdateFunc update_func;

        // 执行当前时间片更新
        inline void update ()
        {
            if ( update_func ) [[likely]]
            {
                update_func ( *this, start_frame, start_ms );
            }
        }
    };

    struct Track
    {
        double FPS = 60.0;   // 默认 60 帧
        utils::PinnedVector< Clip, 32 > clips;

    private:

        // 🚀 核心优化：O(1) 物理帧映射直查表 (LUT)。
        // 索引为绝对帧数，值为该帧下激活的所有 Clip 指针。由于 TinyVector 仅占 8 字节，空间吞吐率极高。
        std::vector< utils::TinyVector< Clip* > > frame_to_clips_lut;

        // 增量式向查找表填充单条 Clip 映射关系
        inline void insertClipToLut ( Clip& clip )
        {
            if ( clip.end_frame >= frame_to_clips_lut.size () )
            {
                frame_to_clips_lut.resize ( clip.end_frame + 1 );
            }
            for ( uint64_t f = clip.start_frame; f <= clip.end_frame; ++f )
            {
                frame_to_clips_lut[ f ].push_back ( &clip );
            }
        }

    public:


        Track () noexcept = default;


        // 🚀 创建纯时间 Clip（从绝对帧数区间创建，自适应 O(1) 增量更新查找表）
        Clip& createClip ( uint64_t start_frame, uint64_t end_frame, UpdateFunc func = nullptr )
        {
            Clip& clip = clips.emplace_back ();
            clip.start_frame = start_frame;
            clip.end_frame = end_frame;

            // 基于 FPS 自动物理对齐毫秒区间
            clip.start_ms = ( static_cast< double > ( start_frame ) / FPS ) * 1000.0;
            clip.end_ms = ( static_cast< double > ( end_frame ) / FPS ) * 1000.0;
            clip.update_func = std::move ( func );

            // 增量更新查找表，时间复杂度仅为 O(duration_frames)
            insertClipToLut ( clip );

            return clip;
        }

        // 🚀 创建纯时间 Clip（从绝对毫秒区间创建，自动换算帧数并增量更新查找表）
        Clip& createClipMs ( double start_ms, double end_ms, UpdateFunc func = nullptr )
        {
            Clip& clip = clips.emplace_back ();
            clip.start_ms = start_ms;
            clip.end_ms = end_ms;

            // 物理反向对齐最邻近帧数区间
            clip.start_frame = static_cast< uint64_t > ( std::round ( ( start_ms / 1000.0 ) * FPS ) );
            clip.end_frame = static_cast< uint64_t > ( std::round ( ( end_ms / 1000.0 ) * FPS ) );
            clip.update_func = std::move ( func );

            insertClipToLut ( clip );

            return clip;
        }

        /**
         * @brief 物理重建整体查找表
         * 适用于用户在外部手动大范围批量修改了已有 Clip 的 start_frame/end_frame 属性后调用
         */
        inline void rebuildLut ()
        {
            frame_to_clips_lut.clear ();
            if ( clips.empty () )
                return;

            uint64_t max_frame = 0;
            for ( const auto& clip : clips )
            {
                max_frame = std::max ( max_frame, clip.end_frame );
            }

            frame_to_clips_lut.resize ( max_frame + 1 );
            for ( auto& clip : clips )
            {
                for ( uint64_t f = clip.start_frame; f <= clip.end_frame; ++f )
                {
                    frame_to_clips_lut[ f ].push_back ( &clip );
                }
            }
        }

        // =====================================================================
        // 🚀 全程无 const 限制、纯可变的极致 O(1) 定位查找接口
        // =====================================================================

        /**
         * @brief 根据给定帧数，O(1) 定位覆盖该帧的第一个可变 Clip
         */
        [[nodiscard]] inline Clip& findClipByFrame ( uint64_t target_frame )
        {
            if ( target_frame >= frame_to_clips_lut.size () || frame_to_clips_lut[ target_frame ].empty () )
                [[unlikely]]
            {
                throw std::out_of_range ( "No active clip covers the target frame." );
            }
            return *frame_to_clips_lut[ target_frame ][ 0 ];
        }

        /**
         * @brief 根据给定毫秒，O(1) 定位覆盖该毫秒的第一个可变 Clip
         */
        [[nodiscard]] inline Clip& findClipByMs ( double target_ms )
        {
            // 通过 FPS 在常数时间内极速对齐至绝对帧
            const uint64_t target_frame = static_cast< uint64_t > ( std::round ( ( target_ms / 1000.0 ) * FPS ) );
            return findClipByFrame ( target_frame );
        }

        /**
         * @brief 检索指定帧下所有活跃的 Clips (支持多轨重叠，0 拷贝返回可变引用)
         * @return 覆盖该帧的 Clips 指针数组的引用，极其高效
         */
        [[nodiscard]] inline utils::TinyVector< Clip* >& findAllActiveClipsByFrame ( uint64_t target_frame )
        {
            if ( target_frame >= frame_to_clips_lut.size () ) [[unlikely]]
            {
                static utils::TinyVector< Clip* > empty_vector;
                return empty_vector;
            }
            return frame_to_clips_lut[ target_frame ];
        }
        inline void runClip ( uint64_t frame )
        {
            // O(1) 零拷贝直接获取当前帧对应的所有活跃 Clip 引用
            auto& active_clips = findAllActiveClipsByFrame ( frame );

            // 顺序调用每一个活跃 Clip 的用户自定义逻辑（自动在 update 内部透传该 Clip 的起始帧和起始毫秒）
            for ( Clip* c : active_clips ) [[likely]]
            {
                c->update ();
            }
        }
    };
}   // namespace StuCanvas
