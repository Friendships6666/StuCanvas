#include "../pch.h"
#include "../include/plot/plotImplicit.h"
#include "../include/functions/lerp.h"
#include "../include/functions/functions.h"

ThreadCacheForTiling::ThreadCacheForTiling() {
    top_row_vals.resize(TILE_W + 1);
    bot_row_vals.resize(TILE_W + 1);
    point_buffer.reserve(3000);
}

void process_tile(const Vec2& world_origin, double wppx, double wppy,
                  const AlignedVector<RPNToken>& rpn_program, const AlignedVector<RPNToken>& rpn_program_check,
                  unsigned int func_idx, unsigned int x_start, unsigned int x_end, unsigned int y_start, unsigned int y_end,
                  ThreadCacheForTiling& cache, oneapi::tbb::concurrent_vector<PointData> & all_points) {
    const unsigned int tile_w = x_end - x_start;
    auto& top_row_vals = cache.top_row_vals; 
    auto& bot_row_vals = cache.bot_row_vals; 
    auto& point_buffer = cache.point_buffer;

    for (unsigned int x = x_start; x <= x_end; ++x) {
        top_row_vals[x - x_start] = evaluate_rpn(rpn_program, world_origin.x + x * wppx, world_origin.y + y_start * wppy);
    }

    for (unsigned int y = y_start; y < y_end; ++y) {
        const std::size_t vec_end = tile_w - (tile_w % batch_type::size);
        for (std::size_t x_off = 0; x_off < vec_end; x_off += batch_type::size) {
            batch_type sx = get_index_vec() + static_cast<double>(x_start + x_off);
            auto [wx, wy] = screen_to_world_batch(sx, (double)y + 1.0, world_origin, wppx, wppy);
            xs::store_aligned(&bot_row_vals[x_off], evaluate_rpn_batch(rpn_program, wx, wy));
        }
        for (std::size_t x_off = vec_end; x_off <= tile_w; ++x_off) {
            Vec2 world_pos = screen_to_world_inline({(double)(x_start + x_off), (double)y + 1.0}, world_origin, wppx, wppy);
            bot_row_vals[x_off] = evaluate_rpn(rpn_program, world_pos.x, world_pos.y);
        }

        point_buffer.clear();
        for (unsigned int x_off = 0; x_off < tile_w; ++x_off) {
            double tl = top_row_vals[x_off], tr = top_row_vals[x_off+1], bl = bot_row_vals[x_off];
            if (!std::isfinite(tl) || !std::isfinite(tr) || !std::isfinite(bl)) continue;
            
            double sign_tl = (tl > 0.0) - (tl < 0.0);
            if (((tr > 0.0) - (tr < 0.0)) == sign_tl && ((bl > 0.0) - (bl < 0.0)) == sign_tl) continue;
            
            constexpr double step = 0.5; 
            Vec2 intersection;
            for (int ly = 0; ly < 2; ++ly) for (int lx = 0; lx < 2; ++lx) {
                Vec2 s_tl_scr = {(double)(x_start + x_off) + lx*step, (double)y + ly*step};
                Vec2 p_tl = screen_to_world_inline(s_tl_scr, world_origin, wppx, wppy);
                Vec2 p_tr = screen_to_world_inline({s_tl_scr.x + step, s_tl_scr.y}, world_origin, wppx, wppy);
                Vec2 p_bl = screen_to_world_inline({s_tl_scr.x, s_tl_scr.y + step}, world_origin, wppx, wppy);
                double v_tl = evaluate_rpn(rpn_program, p_tl.x, p_tl.y);
                double v_tr = evaluate_rpn(rpn_program, p_tr.x, p_tr.y);
                double v_bl = evaluate_rpn(rpn_program, p_bl.x, p_bl.y);
                
                if (!std::isfinite(v_tl) || !std::isfinite(v_tr) || !std::isfinite(v_bl)) continue;
                
                if (try_get_intersection_point(intersection, p_tl, p_tr, v_tl, v_tr, rpn_program_check)) 
                    point_buffer.emplace_back(PointData{intersection, func_idx});
                if (try_get_intersection_point(intersection, p_tl, p_bl, v_tl, v_bl, rpn_program_check)) 
                    point_buffer.emplace_back(PointData{intersection, func_idx});
            }
        }
        if (!point_buffer.empty()) { 
            for(const auto& p : point_buffer) 
                all_points.emplace_back(p); 
        }
        std::swap(top_row_vals, bot_row_vals);
    }
}
