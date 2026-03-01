#include <iostream>
#include <filesystem>
#include <string>
#include <vector>
#include <mutex>
#include <tbb/parallel_for.h>
#include <tbb/global_control.h>

namespace fs = std::filesystem;

// --- 配置区 ---
const std::string INPUT_DIR = "/home/friendships666/PyProject/B站链接/bilibili_videos/264290艺术鉴赏局";
const std::string BGM_PATH = "assets/audios/video/VideoBGM.ogg";
const int CONCURRENT_TASKS = 16;

void process_video(const fs::path& input_path, const fs::path& output_dir) {
    std::string filename = input_path.filename().string();
    fs::path output_path = output_dir / filename;

    // --- 滤镜链逻辑优化 ---
    // 1. crop=...: 使用 trunc(ih*0.9/2)*2 确保高度为偶数，否则 libx264 会直接崩溃报错
    // 2. hflip: 左右镜像
    // 3. format=yuv420p: 强制使用标准 8-bit 像素格式，抹除原片可能存在的 10-bit 或 HDR 特征
    std::string filter = "[0:v]crop=iw:trunc(ih*0.9/2)*2:0:trunc(ih*0.1/2)*2,hflip,format=yuv420p[v]";

    // --- 极致去重指令详解 ---
    // -map_metadata -1: 彻底删掉所有全局标签、创作者信息、拍摄设备、经纬度等。
    // -map_chapters -1: 删掉所有章节标记。
    // -bitexact: 告诉 Muxer（封装器）不要写入版本字符串和 Header 时间戳。
    // -flags +bitexact: 告诉 Encoder（编码器）生成不含特征的原始流。
    // -sn -dn: 删掉可能隐藏在视频里的字幕流和数据流。

    std::string cmd = "ffmpeg -y -i \"" + input_path.string() + "\" "
                      "-stream_loop -1 -i \"" + BGM_PATH + "\" "
                      "-filter_complex \"" + filter + "\" "
                      "-map \"[v]\" -map 1:a -shortest "
                      "-c:v libx264 -preset ultrafast -crf 23 "
                      "-c:a aac -b:a 128k "
                      "-map_metadata -1 -map_chapters -1 "
                      "-fflags +bitexact -flags:v +bitexact -flags:a +bitexact -bitexact "
                      "-sn -dn "
                      "\"" + output_path.string() + "\" > /dev/null 2>&1";

    {
        static std::mutex log_mtx;
        std::lock_guard<std::mutex> lock(log_mtx);
        std::cout << "[CPU-WASH] 正在深度清理指纹: " << filename << std::endl;
    }

    int ret = std::system(cmd.c_str());

    if (ret != 0) {
        std::cerr << "[ERROR] 失败: " << filename << " (请检查输出路径或视频格式)" << std::endl;
    }
}

int main() {
    fs::path in_p(INPUT_DIR);
    if (!fs::exists(in_p)) {
        std::cerr << "输入目录不存在: " << INPUT_DIR << std::endl;
        return -1;
    }

    std::string out_name = in_p.filename().string() + "_Step1";
    fs::path out_p = in_p.parent_path() / out_name;

    if (!fs::exists(out_p)) {
        fs::create_directories(out_p);
    }

    std::vector<fs::path> video_files;
    for (const auto& entry : fs::directory_iterator(in_p)) {
        if (entry.path().extension() == ".mp4") {
            video_files.push_back(entry.path());
        }
    }

    std::cout << "找到 " << video_files.size() << " 个视频，开始执行 TBB 并行洗白..." << std::endl;

    // TBB 并行计算引擎
    tbb::global_control control(tbb::global_control::max_allowed_parallelism, CONCURRENT_TASKS);

    tbb::parallel_for(tbb::blocked_range<size_t>(0, video_files.size()),
        [&](const tbb::blocked_range<size_t>& r) {
            for (size_t i = r.begin(); i != r.end(); ++i) {
                process_video(video_files[i], out_p);
            }
        });

    std::cout << "\n--- CPU 版本全量洗白任务已完成 ---" << std::endl;
    std::cout << "所有视频均已镜像、切头、抹除元数据并替换音频。" << std::endl;

    return 0;
}