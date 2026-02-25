#include <random>
#include <iomanip>
#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <set> // 必须包含
#include <filesystem> // 必须包含
#include "video/config.hpp"
#include "video/render_utils.hpp"
#include "video/ffmpeg_utils.hpp"
#include "video/physics_utils.hpp"
#include <vector>
#include <string>

std::vector<std::string> title_pool = {
    // --- 互动与悬念类 (52个标题) ---
    "猜猜看：哪个颜色的小球能第一个逃出圆环？",
    "逃离挑战：多层旋转圆环，小球能否绝处逢生？",
    "概率实验：这些小球里，谁才是真正的天选之子？",
    "视觉悬念：层层围堵之下，小球奇迹会出现吗？",
    "终极逃亡：圆环不断收缩，留给小球的时间不多了",
    "极致解压：感受纯粹的物理碰撞，强迫症福音",
    "丝滑体验：感受这些彩色线条流动带来的美感",
    "深度治愈：听听这碰撞声，这才是真正的解压视频",
    "视觉洗脑：循环往复的运动，让大脑彻底放松",
    "极度舒适：当物理模拟达到某种平衡，你会发现美",
    "数学之美：完全弹性碰撞下的能量守恒模拟实验",
    "疯狂碰撞：小球的绝地突围，这走位太真实了！",
    "空间压缩：不断缩小的圆环，小球该何去何从？",
    "极速生存：当速度达到临界点，碰撞即是艺术",
    "颜色狂欢：每一帧都是壁纸，这渲染效果太震撼",
    "无法预料：物理定律决定结局，过程却充满惊喜",
    "几何奥秘：旋转的开口，寻找那一瞬间的生机",
    "今天的幸运小球是谁？看碰撞结果就知道了！",
    "别眨眼：小球逃离的瞬间，就是奇迹发生的时刻",
    "模拟实验室：不同重力下，逃离速度的差异实验",
    "强迫症慎入：当小球卡在开口处，你会抓狂吗？",
    "逃出生天：这是一场关于概率与几何的视觉盛宴",
    "蓝色还是红色？评论区押注，看谁能笑到最后！",
    "这次我赌绿色球！快来看看你的直觉到底准不准",
    "全场唯一的希望！这个幸存球能否成功突破重围？",
    "命运的转折点，往往就隐藏在那个瞬间的开口处",
    "别走开，最后五秒钟带你见证真正的天选时刻",
    "关于运气的终极实验：谁能率先穿越这五层封锁？",
    "深夜解压必备：看这些彩色圆点跳动真的很治愈",
    "放空大脑，沉浸在无限循环的轨迹美学之中",
    "极度舒适：每一个碰撞点都精准得让人头皮发麻",
    "抛开烦恼，静静观察这场没有尽头的几何律动",
    "视觉按摩：看着小球在圆环间穿梭，焦虑瞬间消失",
    "这绝对是强迫症最想看到的画面，建议反复观看",
    "绝境生存：当所有开口都错开的瞬间，看得太揪心了",
    "穿针引线般的走位，这真的只是随机运动的结果吗？",
    "四周都是绝路，这个小球竟然靠反弹找到了生机",
    "惊险万分！只差一毫米就卡住了，看得我手心冒汗",
    "突破层层防线，这可能是目前最难的一次逃脱挑战",
    "永不言弃：看彩色小球如何在这座迷宫中寻找出口",
    "秩序与混乱的博弈：看起来杂乱，实际充满了规律",
    "假如重力变得微弱，这场逃离比赛会发生什么变化？",
    "为什么小球总是撞向同一个地方？背后的逻辑很有趣",
    "当碰撞不再有能量损耗，你会看到一场动量盛宴",
    "观察重力下的完美抛物线：这才是最真实的力量感",
    "碰撞的极限：如果圆环无限缩小，最终结局会怎样？",
    "每个小球都在寻找出口，像极了在生活中努力的我们",
    "茫茫色块中的那一点亮色，就是突破封锁的曙光",
    "给心灵放个假：在彩色的世界里看一场华丽的突围",
    "如果物理世界有颜色，那一定会像这样绚烂且治愈",
    "时间在旋转中悄悄流逝，而小球从未停止追寻自由",
    "最后一秒的惊人反转：永远不要低估任何一种可能"
};

// 将单次运行封装成函数
void run_simulation(const ScenarioConfig& cfg, std::vector<ImpactEvent>& impacts) {
    b2WorldDef worldDef = b2DefaultWorldDef();
    worldDef.gravity = {0.0f, cfg.gravity};
    b2WorldId worldId = b2CreateWorld(&worldDef);
    b2World_SetRestitutionThreshold(worldId, 0.001f);

    b2SurfaceMaterial mat = b2DefaultSurfaceMaterial();
    mat.restitution = 1.0f;
    mat.friction = 0.0f;

    struct InternalRing { b2BodyId bodyId; float radius; float angle; uint32_t color; };
    std::vector<InternalRing> rings;
    uint32_t palette[] = {0xFF0055, 0xFF8800, 0xFFEE00, 0x00FF88, 0x00CCFF, 0xAA00FF, 0x00FFFF};

    for (int i = 0; i < cfg.ring_layers; ++i) {
        b2BodyDef bd = b2DefaultBodyDef();
        bd.type = b2_kinematicBody;
        bd.position = {5.4f, 9.6f}; // 屏幕中心 (10.8/2, 19.2/2)
        b2BodyId bid = b2CreateBody(worldId, &bd);

        float r = cfg.base_radius + i * cfg.spacing;
        float start_angle = (float)(rand() % 360) * (M_PI / 180.0f);

        Physics::create_thick_ring_layer(worldId, bid, r, cfg.ring_thickness, cfg.hole_angle, i, &mat);
        b2Body_SetTransform(bid, bd.position, b2MakeRot(start_angle));
        rings.push_back({bid, r, start_angle, palette[i % 7]});
    }

    // 创建小球
    std::vector<Ball> balls;
    b2ShapeDef ballShape = b2DefaultShapeDef();
    ballShape.material = mat;
    ballShape.enableHitEvents = true;
    // 【关键修复】：加上类别和掩码，否则圆环不认识小球！
    ballShape.filter.categoryBits = CATEGORY_BALL;
    ballShape.filter.maskBits = CATEGORY_RING; // 球只和环碰撞，球与球之间互相穿透
    b2Circle circle = {{0.0f, 0.0f}, 0.1f};

    for (int i = 0; i < cfg.ball_count; ++i) {
        b2BodyDef bd = b2DefaultBodyDef();
        bd.type = b2_dynamicBody;
        bd.position = {5.4f, 9.6f};
        bd.isBullet = true;
        b2BodyId bid = b2CreateBody(worldId, &bd);
        ballShape.userData = (void*)(intptr_t)i;
        b2CreateCircleShape(bid, &ballShape, &circle);

        float a = (i * (360.0f / cfg.ball_count)) * (M_PI / 180.0f);
        b2Body_SetLinearVelocity(bid, {7.0f * cosf(a), 0.01f * sinf(a)});
        balls.push_back({bid, 0xFFFFFF, -1.0f});
    }

    VideoEncoder encoder(cfg.temp_video_name);
    std::vector<uint8_t> super_buf(SUPER_WIDTH * SUPER_HEIGHT * 4);
    std::vector<uint8_t> out_buf(WIDTH * HEIGHT * 4);

    for (int f = 0; f < TOTAL_FRAMES; ++f) {
        b2World_Step(worldId, 1.0f / FPS, 8);
        b2ContactEvents ev = b2World_GetContactEvents(worldId);

        for (int i = 0; i < ev.hitCount; ++i) {
            b2ContactHitEvent* h = ev.hitEvents + i;
            int rIdx = -1, bIdx = -1;
            int valA = (int)(intptr_t)b2Shape_GetUserData(h->shapeIdA);
            int valB = (int)(intptr_t)b2Shape_GetUserData(h->shapeIdB);

            if (valA >= 100) rIdx = valA - 100; else bIdx = valA;
            if (valB >= 100) rIdx = valB - 100; else bIdx = valB;

            if (rIdx >= 0 && bIdx >= 0 && bIdx < cfg.ball_count) {
                float now = (float)f / FPS;
                if (now - balls[bIdx].last_hit_time > 0.02f) {
                    float dir = (rIdx % 2 == 0) ? 1.0f : -1.0f;
                    rings[rIdx].angle += dir * (12.0f * M_PI / 180.0f);
                    b2Body_SetTransform(rings[rIdx].bodyId, {5.4f, 9.6f}, b2MakeRot(rings[rIdx].angle));
                    balls[bIdx].color = rings[rIdx].color;
                    balls[bIdx].last_hit_time = now;
                    impacts.push_back({now, std::clamp(h->approachSpeed / 15.0f, 0.1f, 1.0f)});
                }
            }
        }

        // 渲染部分 (TBB)
        Render::clear_buffer(super_buf, 10, 10, 15);
        for(auto& r : rings) Render::draw_arc_super(super_buf, SUPER_WIDTH/2, SUPER_HEIGHT/2, r.radius*PTM_RATIO*SSAA_FACTOR, cfg.ring_thickness*PTM_RATIO*SSAA_FACTOR, cfg.hole_angle, r.angle, r.color);
        for(auto& b : balls) {
            b2Vec2 p = b2Body_GetPosition(b.id);
            Render::draw_solid_circle_super(super_buf, p.x*PTM_RATIO*SSAA_FACTOR, p.y*PTM_RATIO*SSAA_FACTOR, 10*SSAA_FACTOR, b.color);
        }
        Render::downsample_buffer(super_buf, out_buf);
        encoder.encode(out_buf, f);
    }
    b2DestroyWorld(worldId);
}
namespace fs = std::filesystem;

// 线程安全的文件名追踪
std::set<std::string> used_titles;
std::mutex used_titles_mtx;
std::mutex console_mtx;

/**
 * @brief 清理标题中的非法字符
 */
std::string sanitize_filename(std::string name) {
    std::replace(name.begin(), name.end(), '/', '_');
    std::replace(name.begin(), name.end(), '\'', '_');
    // 移除末尾可能的空格
    name.erase(std::find_if(name.rbegin(), name.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), name.end());
    return name;
}

/**
 * @brief 从池中寻找一个唯一的标题
 */
std::string get_unique_title(int task_id, std::mt19937& rng) {
    std::lock_guard<std::mutex> lock(used_titles_mtx);

    // 1. 打乱索引尝试寻找未使用的标题
    std::vector<size_t> indices(title_pool.size());
    std::iota(indices.begin(), indices.end(), 0);
    std::shuffle(indices.begin(), indices.end(), rng);

    for (size_t idx : indices) {
        std::string candidate = title_pool[idx];
        if (used_titles.find(candidate) == used_titles.end()) {
            used_titles.insert(candidate);
            return candidate;
        }
    }

    // 2. 如果池子用光了（极端情况），则回退到带数字模式
    return title_pool[task_id % title_pool.size()] + "_" + std::to_string(task_id);
}

int main() {
    const std::string BASE_OUT_PATH = "/home/friendships666/StuCanvasVideo/RingAndBalls/";
    const int TOTAL_VIDEOS = 20;

    // 1. 确保输出目录存在
    try {
        if (!fs::exists(BASE_OUT_PATH)) {
            fs::create_directories(BASE_OUT_PATH);
            std::cout << "[System] 创建输出目录: " << BASE_OUT_PATH << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "[Fatal] 目录创建失败: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "=========================================================" << std::endl;
    std::cout << "StuCanvas 视频自动化流水线 (顺序执行模式)" << std::endl;
    std::cout << "总任务量: " << TOTAL_VIDEOS << " 视频" << std::endl;
    std::cout << "模式说明: 顺序压制以节省内存，防止 Kernel OOM 弹出" << std::endl;
    std::cout << "=========================================================\n" << std::endl;

    // 使用统一的随机数引擎
    std::mt19937 rng(static_cast<unsigned int>(std::time(0)));

    // 2. 开始顺序循环
    for (int i = 1; i <= TOTAL_VIDEOS; ++i) {
        // 计算百分比进度
        float total_progress = (static_cast<float>(i - 1) / TOTAL_VIDEOS) * 100.0f;

        std::cout << "\n>>> [整体进度: " << std::fixed << std::setprecision(1) << total_progress << "%] "
                  << "正在处理第 " << i << " / " << TOTAL_VIDEOS << " 个视频..." << std::endl;

        // A. 配置生成
        ScenarioConfig cfg;
        cfg.id = i;

        // 获取唯一标题 (内部已处理去重逻辑)
        cfg.video_title = get_unique_title(i, rng);

        // 随机参数配置
        cfg.ring_layers    = std::uniform_int_distribution<int>(3, 6)(rng);
        cfg.ball_count     = std::uniform_int_distribution<int>(20, 50)(rng);
        cfg.ring_thickness = std::uniform_real_distribution<float>(0.20f, 0.35f)(rng);
        cfg.hole_angle     = std::uniform_real_distribution<float>(0.25f, 0.40f)(rng);
        cfg.gravity        = std::uniform_real_distribution<float>(0.01f, 0.01f)(rng);
        cfg.base_radius    = 1.2f;
        cfg.spacing        = (4.8f - cfg.base_radius) / static_cast<float>(cfg.ring_layers);

        // B. 文件名生成
        std::string safe_title = sanitize_filename(cfg.video_title);
        cfg.temp_video_name  = "temp_main_worker.mp4"; // 顺序执行可以复用同一个名字
        cfg.final_video_name = BASE_OUT_PATH + safe_title + ".mp4";

        std::cout << "   [标题]: " << cfg.video_title << std::endl;
        std::cout << "   [参数]: 层数=" << cfg.ring_layers << ", 球数=" << cfg.ball_count << std::endl;

        // C. 执行模拟 (内部会打印 f 帧进度)
        std::vector<ImpactEvent> impacts;

        // 因为是单线程顺序执行，我们可以直接在控制台实时看到 run_simulation 内部的输出
        run_simulation(cfg, impacts);

        // D. 音频混合与文字合成
        std::cout << "\n   [合成]: 正在进行音频混合与最终压制..." << std::endl;
        VideoEncoder::mix_audio(impacts, cfg);

        // E. 清理临时视频
        if (fs::exists(cfg.temp_video_name)) {
            fs::remove(cfg.temp_video_name);
        }

        std::cout << "   [成功]: 视频保存至 -> " << safe_title << ".mp4" << std::endl;
    }

    std::cout << "\n=========================================================" << std::endl;
    std::cout << "所有 30 个视频处理完毕！" << std::endl;
    std::cout << "输出目录: " << BASE_OUT_PATH << std::endl;
    std::cout << "=========================================================" << std::endl;

    return 0;
}