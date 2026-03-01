#include <opencv2/opencv.hpp>
#include <opencv2/objdetect.hpp>
#include <iostream>
#include <filesystem>
#include <vector>
#include <string>
#include <mutex>
#include <tbb/parallel_for.h>
#include <tbb/global_control.h>

namespace fs = std::filesystem;

// --- 配置区 ---
const std::string INPUT_DIR = "/home/friendships666/PyProject/B站链接/bilibili_videos/264290艺术鉴赏局_Step1";
const std::string MODEL_PATH = "/home/friendships666/Projects/WASMTest/models/yunet.onnx";
const int CONCURRENT_TASKS = 8;     // 同时处理的视频数
const int MAX_MEMORY_FRAMES = 20;   // 脸部消失后，模糊块继续保留的帧数
const float EXPAND_RATE = 1.02;    // 丢失目标时，每帧模糊块自动扩大的比例

std::mutex log_mtx;

struct FaceState {
    cv::Rect rect;
    int life;
};

// 强力“下毒”级模糊
void apply_heavy_desensitization(cv::Mat& roi) {
    if (roi.empty()) return;
    int sw = std::max(8, roi.cols / 10);
    int sh = std::max(8, roi.rows / 10);
    cv::Mat small;
    cv::resize(roi, small, cv::Size(sw, sh), 0, 0, cv::INTER_NEAREST);
    cv::resize(small, roi, roi.size(), 0, 0, cv::INTER_LINEAR);
    int ksize = (roi.cols * 0.6);
    if (ksize % 2 == 0) ksize++;
    if (ksize < 3) ksize = 3;
    cv::GaussianBlur(roi, roi, cv::Size(ksize, ksize), 50);
}

void process_video_task(const fs::path& input_path, const fs::path& output_path) {
    std::string filename = input_path.filename().string();
    cv::VideoCapture cap(input_path.string());
    if (!cap.isOpened()) return;

    int width = (int)cap.get(cv::CAP_PROP_FRAME_WIDTH);
    int height = (int)cap.get(cv::CAP_PROP_FRAME_HEIGHT);
    double fps = cap.get(cv::CAP_PROP_FPS);

    auto detector = cv::FaceDetectorYN::create(MODEL_PATH, "", cv::Size(width, height), 0.7f, 0.3f, 5000);

    std::string temp_video = output_path.string() + ".tmp.mp4";
    cv::VideoWriter writer(temp_video, cv::VideoWriter::fourcc('a', 'v', 'c', '1'), fps, cv::Size(width, height));

    std::vector<FaceState> face_memories;
    cv::Mat frame;

    while (cap.read(frame)) {
        if (frame.empty()) break;
        cv::Mat faces;
        detector->setInputSize(frame.size());
        detector->detect(frame, faces);

        if (faces.rows > 0) {
            face_memories.clear();
            for (int i = 0; i < faces.rows; i++) {
                cv::Rect r(faces.at<float>(i, 0), faces.at<float>(i, 1), faces.at<float>(i, 2), faces.at<float>(i, 3));
                face_memories.push_back({r, MAX_MEMORY_FRAMES});
            }
        } else {
            for (auto& fm : face_memories) {
                if (fm.life > 0) {
                    fm.life--;
                    float dx = fm.rect.width * (EXPAND_RATE - 1.0f) / 2.0f;
                    float dy = fm.rect.height * (EXPAND_RATE - 1.0f) / 2.0f;
                    fm.rect.x -= dx;
                    fm.rect.y -= dy;
                    fm.rect.width *= EXPAND_RATE;
                    fm.rect.height *= EXPAND_RATE;
                }
            }
        }

        for (auto& fm : face_memories) {
            if (fm.life > 0) {
                cv::Rect safe_rect = fm.rect & cv::Rect(0, 0, frame.cols, frame.rows);
                if (safe_rect.width > 0 && safe_rect.height > 0) {
                    cv::Mat roi = frame(safe_rect);
                    apply_heavy_desensitization(roi);
                }
            }
        }
        writer.write(frame);
    }

    cap.release();
    writer.release();

    std::string cmd = "LC_ALL=C.UTF-8 ffmpeg -y -i \"" + temp_video + "\" -i \"" + input_path.string() + "\" "
                      "-map 0:v:0 -map 1:a:0 -c copy -map_metadata -1 -bitexact "
                      "\"" + output_path.string() + "\" > /dev/null 2>&1";

    std::system(cmd.c_str());
    fs::remove(temp_video);

    std::lock_guard<std::mutex> lock(log_mtx);
    std::cout << "[SUCCESS] Step2: " << filename << std::endl;
}

int main() {
    // 1. 路径逻辑
    fs::path in_p(INPUT_DIR);
    if (!fs::exists(in_p)) {
        std::cerr << "输入目录不存在！" << std::endl;
        return -1;
    }

    std::string out_name = in_p.filename().string();
    size_t pos = out_name.find("_Step1");
    if (pos != std::string::npos) {
        out_name.replace(pos, 6, "_Step2");
    } else {
        out_name += "_Step2";
    }

    fs::path out_p = in_p.parent_path() / out_name;
    if (!fs::exists(out_p)) fs::create_directories(out_p);

    if (!fs::exists(MODEL_PATH)) {
        std::cerr << "找不到模型文件: " << MODEL_PATH << std::endl;
        return -1;
    }

    // 2. 收集并过滤视频列表 (跳过已存在的文件)
    std::vector<fs::path> video_files;
    int skip_count = 0;

    for (const auto& entry : fs::directory_iterator(in_p)) {
        if (entry.path().extension() == ".mp4") {
            fs::path target_file = out_p / entry.path().filename();

            // 核心逻辑：如果目标文件已存在，则不加入待处理任务列表
            if (fs::exists(target_file)) {
                skip_count++;
                continue;
            }
            video_files.push_back(entry.path());
        }
    }

    std::cout << "--- StuCanvas Step2: 高强度抗检测人脸脱敏启动 ---" << std::endl;
    std::cout << "目标目录: " << out_p << std::endl;
    std::cout << "已跳过任务 (已存在): " << skip_count << std::endl;
    std::cout << "待处理任务数: " << video_files.size() << std::endl;

    if (video_files.empty()) {
        std::cout << "没有需要处理的新视频。程序退出。" << std::endl;
        return 0;
    }

    // 3. TBB 并发流水线
    tbb::global_control control(tbb::global_control::max_allowed_parallelism, CONCURRENT_TASKS);
    tbb::parallel_for(tbb::blocked_range<size_t>(0, video_files.size()),
        [&](const tbb::blocked_range<size_t>& r) {
            for (size_t i = r.begin(); i != r.end(); ++i) {
                fs::path out_file = out_p / video_files[i].filename();
                process_video_task(video_files[i], out_file);
            }
        });

    std::cout << "\n[DONE] 所有视频已完成 Step2 脱敏。" << std::endl;
    return 0;
}