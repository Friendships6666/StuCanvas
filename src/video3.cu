#include <iostream>
#include <vector>
#include <filesystem>
#include <cuda_runtime.h>
#include <cufft.h>
#include <npp.h>
#include <curand_kernel.h>
#include <opencv2/opencv.hpp>
#include <mutex>
#include <tbb/parallel_for.h>
#include <tbb/global_control.h>

namespace fs = std::filesystem;

// --- 生产环境配置 ---
const std::string INPUT_DIR = "/home/friendships666/PyProject/B站链接/bilibili_videos/694763095小追让你心情愉悦_Step1";
const std::string OUTPUT_SUFFIX = "_Step3";
const std::string FONT_PATH = "/home/friendships666/Projects/WASMTest/assets/fonts/NotoSansSC-Regular.ttf";
const std::string TEXT_PATH = "/home/friendships666/Projects/WASMTest/assets/texts/simd_article.txt";
const std::string OUTRO_IMG_PATH = "/home/friendships666/Projects/WASMTest/assets/images/video/simd.png";
const int CONCURRENT_TASKS = 4;

std::mutex log_mtx;

#define CUDA_CHECK(call) { \
    cudaError_t err = call; \
    if(err != cudaSuccess) { \
        std::lock_guard<std::mutex> lock(log_mtx); \
        std::cerr << "❌ CUDA Error: " << cudaGetErrorString(err) << " at line " << __LINE__ << std::endl; \
    } \
}
#define NPP_CHECK(call) { \
NppStatus stat = call; \
if(stat != NPP_SUCCESS) { \
std::lock_guard<std::mutex> lock(log_mtx); \
std::cerr << "❌ NPP Error: " << stat << " at line " << __LINE__ << std::endl; \
} \
}
// --- CUDA 核函数部分 (省略重复的 Phase 和 Moire 核函数，保持逻辑一致) ---
__global__ void poison_phase_kernel(cufftComplex* data, int width, int height, float seed) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    int fft_w = width / 2 + 1;
    if (x < fft_w && y < height) {
        int idx = y * fft_w + x;
        curandState state;
        curand_init((unsigned long long)seed, idx, 0, &state);
        float jitter = (curand_uniform(&state) - 0.5f) * 0.3f;
        float re = data[idx].x;
        float im = data[idx].y;
        float mag = sqrtf(re * re + im * im);
        float phase = atan2f(im, re) + jitter;
        data[idx].x = mag * cosf(phase);
        data[idx].y = mag * sinf(phase);
    }
}

__global__ void apply_warped_moire_kernel(const uint8_t* d_src, uint8_t* d_dst, int width, int height, int step, int frame_idx) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x < width && y < height) {
        float t = (float)frame_idx * 0.08f;
        float off_x = sinf((float)y * 0.01f + t) * 1.0f;
        float off_y = cosf((float)x * 0.008f + t * 1.1f) * 1.0f;
        float sx = fmaxf(0.0f, fminf((float)x + off_x, (float)width - 1.001f));
        float sy = fmaxf(0.0f, fminf((float)y + off_y, (float)height - 1.001f));
        int x0 = (int)sx; int y0 = (int)sy;
        float dx = sx - (float)x0; float dy = sy - (float)y0;
        int pattern = ((x + y + frame_idx) & 1) == 0 ? 2 : -2;
        for (int c = 0; c < 3; ++c) {
            float p00 = d_src[y0 * step + x0 * 3 + c];
            float p10 = d_src[y0 * step + (x0+1) * 3 + c];
            float p01 = d_src[(y0+1) * step + x0 * 3 + c];
            float p11 = d_src[(y0+1) * step + (x0+1) * 3 + c];
            float interpolated = (p00 * (1.0f - dx) + p10 * dx) * (1.0f - dy) + (p01 * (1.0f - dx) + p11 * dx) * dy;
            d_dst[y * step + x * 3 + c] = (uint8_t)fmaxf(0.0f, fminf(255.0f, interpolated + (float)pattern));
        }
    }
}
void initNppContext(NppStreamContext& ctx) {
    memset(&ctx, 0, sizeof(ctx));
    ctx.hStream = 0;
    CUDA_CHECK(cudaGetDevice(&ctx.nCudaDeviceId));
    cudaDeviceGetAttribute(&ctx.nMultiProcessorCount, cudaDevAttrMultiProcessorCount, ctx.nCudaDeviceId);
    cudaDeviceGetAttribute(&ctx.nMaxThreadsPerMultiProcessor, cudaDevAttrMaxThreadsPerMultiProcessor, ctx.nCudaDeviceId);
    cudaDeviceGetAttribute(&ctx.nMaxThreadsPerBlock, cudaDevAttrMaxThreadsPerBlock, ctx.nCudaDeviceId);
}
class BlackwellFullToxinEngine {
public:
    BlackwellFullToxinEngine(int w, int h) : width(w), height(h) {
        size_t sz = width * height;
        cudaMalloc(&d_bgr, sz * 3); cudaMalloc(&d_final, sz * 3);
        cudaMalloc(&d_r, sz); cudaMalloc(&d_g, sz); cudaMalloc(&d_b, sz);
        cudaMalloc(&d_b_float, sz * sizeof(float));
        cudaMalloc(&d_b_complex, height * (width / 2 + 1) * sizeof(cufftComplex));
        cufftPlan2d(&plan_fwd, height, width, CUFFT_R2C);
        cufftPlan2d(&plan_inv, height, width, CUFFT_C2R);
        memset(&nppCtx, 0, sizeof(nppCtx));
        cudaGetDevice(&nppCtx.nCudaDeviceId);
    }
    ~BlackwellFullToxinEngine() {
        cudaFree(d_bgr); cudaFree(d_final); cudaFree(d_r); cudaFree(d_g); cudaFree(d_b);
        cudaFree(d_b_float); cudaFree(d_b_complex);
        cufftDestroy(plan_fwd); cufftDestroy(plan_inv);
    }
    void process(cv::Mat& frame, float seed, int frame_idx) {
        if (!frame.isContinuous()) frame = frame.clone();
        size_t sz = width * height;
        NppiSize roi = { width, height };
        cudaMemcpy(d_bgr, frame.data, sz * 3, cudaMemcpyHostToDevice);
        Npp8u* planes[3] = { d_b, d_g, d_r };
        nppiCopy_8u_C3P3R_Ctx(d_bgr, frame.step, planes, width, roi, nppCtx);
        nppiConvert_8u32f_C1R_Ctx(d_b, width, d_b_float, width * sizeof(float), roi, nppCtx);
        cufftExecR2C(plan_fwd, (cufftReal*)d_b_float, d_b_complex);
        poison_phase_kernel<<<dim3(((width/2+1)+15)/16, (height+15)/16), dim3(16, 16)>>>(d_b_complex, width, height, seed);
        cufftExecC2R(plan_inv, d_b_complex, (cufftReal*)d_b_float);
        nppiMulC_32f_C1IR_Ctx(1.0f / (width * height), d_b_float, width * sizeof(float), roi, nppCtx);
        nppiConvert_32f8u_C1R_Ctx(d_b_float, width * sizeof(float), d_b, width, roi, NPP_RND_NEAR, nppCtx);
        nppiCopy_8u_P3C3R_Ctx(planes, width, d_bgr, frame.step, roi, nppCtx);
        apply_warped_moire_kernel<<<dim3((width+15)/16, (height+15)/16), dim3(16, 16)>>>(d_bgr, d_final, width, height, (int)frame.step, frame_idx);
        cudaMemcpy(frame.data, d_final, sz * 3, cudaMemcpyDeviceToHost);
    }
private:
    int width, height; cufftHandle plan_fwd, plan_inv;
    Npp8u *d_bgr, *d_final, *d_r, *d_g, *d_b; float *d_b_float; cufftComplex *d_b_complex; NppStreamContext nppCtx;
};

// --- Outro 生成助手：黑屏 + 比例缩放居中图片 ---
void append_outro(cv::VideoWriter& writer, int w, int h, int fps) {
    cv::Mat outro_frame = cv::Mat::zeros(h, w, CV_8UC3);
    cv::Mat simd_img = cv::imread(OUTRO_IMG_PATH);
    if (!simd_img.empty()) {
        double scale = std::min((double)w / simd_img.cols, (double)h / simd_img.rows);
        int nw = (int)(simd_img.cols * scale);
        int nh = (int)(simd_img.rows * scale);
        cv::Mat resized;
        cv::resize(simd_img, resized, cv::Size(nw, nh));

        int x_off = (w - nw) / 2;
        int y_off = (h - nh) / 2;
        resized.copyTo(outro_frame(cv::Rect(x_off, y_off, nw, nh)));
    }
    // 写入 10 秒
    for (int i = 0; i < 1 * fps; ++i) {
        writer.write(outro_frame);
    }
}

void run_task(const fs::path& input, const fs::path& output) {
    cv::VideoCapture cap(input.string());
    if(!cap.isOpened()) return;
    int w = (int)cap.get(cv::CAP_PROP_FRAME_WIDTH);
    int h = (int)cap.get(cv::CAP_PROP_FRAME_HEIGHT);
    double fps = cap.get(cv::CAP_PROP_FPS);

    BlackwellFullToxinEngine engine(w, h);
    float base_seed = (float)(std::hash<std::string>{}(input.filename().string()) % 77777);

    std::string tmp = output.string() + ".raw.avi";
    cv::VideoWriter writer(tmp, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), fps, cv::Size(w, h));

    cv::Mat frame;
    int idx = 0;
    while (cap.read(frame)) {
        engine.process(frame, base_seed + (float)idx, idx);
        writer.write(frame);
        idx++;
    }

    // --- 注入 10 秒 Outro ---
    append_outro(writer, w, h, (int)fps);

    cap.release(); writer.release();

    // --- 核心滤镜配置 ---
    // 1. setpts: 剧烈时序下毒
    // 2. expansion=none: 修复 % 报错
    // 3. 提高滚动速度，使 256 行文字能快速刷完
    std::string temporal_filter = "setpts='PTS*(1+0.10*sin(2*PI*T/4))'";
    std::string ocr_filter = "drawtext=fontfile='" + FONT_PATH + "':"
                             "textfile='" + TEXT_PATH + "':"
                             "fontcolor=white@0.02:fontsize=45:line_spacing=2:"
                             "expansion=none:"
                             "x=30:y=h-60*t";

    std::string filter_complex = "[0:v]" + temporal_filter + "," + ocr_filter + "[vout];"
                                 "[1:a]asetpts='PTS*(1+0.10*sin(2*PI*T/4))',atempo=1.05[aout]";

    // 注意：去掉 -shortest 确保视频完整显示 10s Outro，哪怕音频已结束。
    std::string cmd = "LD_LIBRARY_PATH=/opt/cuda/lib64:/usr/lib:$LD_LIBRARY_PATH "
                      "LC_ALL=C.UTF-8 ffmpeg -y -i \"" + tmp + "\" -i \"" + input.string() + "\" "
                      "-filter_complex \"" + filter_complex + "\" "
                      "-map \"[vout]\" -map \"[aout]\"? " // [aout]? 表示音频可选，防止结尾10s无音轨报错
                      "-c:v libx264 -preset ultrafast -crf 19 "
                      "-map_metadata -1 -map_chapters -1 -bitexact "
                      "\"" + output.string() + "\" > /dev/null 2>&1";

    std::system(cmd.c_str());
    fs::remove(tmp);

    std::lock_guard<std::mutex> lock(log_mtx);
    std::cout << "💀 [全维度劫持+拉长10秒完成] " << input.filename() << std::endl;
}

int main() {
    fs::path in_p(INPUT_DIR);
    std::string out_p_str = in_p.parent_path().string() + "/" + in_p.filename().string() + OUTPUT_SUFFIX;
    fs::path out_p(out_p_str);
    if (!fs::exists(out_p)) fs::create_directories(out_p);

    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(in_p)) {
        if (e.path().extension() == ".mp4") files.push_back(e.path());
    }

    tbb::global_control control(tbb::global_control::max_allowed_parallelism, CONCURRENT_TASKS);
    tbb::parallel_for(tbb::blocked_range<size_t>(0, files.size()), [&](const tbb::blocked_range<size_t>& r) {
        for (size_t i = r.begin(); i != r.end(); ++i) {
            run_task(files[i], out_p / files[i].filename());
        }
    });

    return 0;
}