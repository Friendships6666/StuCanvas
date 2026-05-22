// stucanvas/canvas/vulkan/nvenc_cuda_encoder.hpp
#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <stdexcept>
#include <unordered_map>
#include <vulkan/vulkan.h>

#ifdef _WIN32
#include <vulkan/vulkan_win32.h>
#endif

// ============================================================================
// 1. 跨平台动态库载入定义与宏
// ============================================================================
#if defined(_WIN32)
#include <windows.h>
typedef HMODULE LibraryHandle;
#define LOAD_LIBRARY(name) LoadLibraryA(name)
#define GET_SYMBOL(handle, name) GetProcAddress(handle, name)
#define FREE_LIBRARY(handle) FreeLibrary(handle)
#else
#include <dlfcn.h>
typedef void* LibraryHandle;
#define LOAD_LIBRARY(name) dlopen(name, RTLD_NOW)
#define GET_SYMBOL(handle, name) dlsym(handle, name)
#define FREE_LIBRARY(handle) dlclose(handle)
#endif

// ============================================================================
// 2. CUDA 驱动 API 数据结构（若系统未引入 cuda.h 时进行自适应定义）
// ============================================================================
#ifndef __cuda_cuda_h__
#ifndef CUDA_VERSION
typedef unsigned long long CUdeviceptr;
typedef struct CUctx_st* CUcontext;
typedef struct CUstream_st* CUstream;
typedef int CUdevice;
typedef struct CUextMemory_st* CUexternalMemory;
typedef struct CUextSemaphore_st* CUexternalSemaphore;

typedef enum cudaError_enum {
    CUDA_SUCCESS = 0
} CUresult;

#ifndef CUDAAPI
#ifdef _WIN32
#define CUDAAPI __stdcall
#else
#define CUDAAPI
#endif
#endif

typedef enum CUexternalMemoryHandleType_enum {
    CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD = 1,
    CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32 = 2,
    CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_KMT = 3
} CUexternalMemoryHandleType;

typedef struct CUDA_EXTERNAL_MEMORY_HANDLE_DESC_st {
    CUexternalMemoryHandleType type;
    union {
        int fd;
        struct {
            void *handle;
            const void *name;
        } win32;
        const void *nvSciBufObject;
    } handle;
    unsigned long long size;
    unsigned int flags;
    unsigned int reserved[16];
} CUDA_EXTERNAL_MEMORY_HANDLE_DESC;

typedef struct CUDA_EXTERNAL_MEMORY_BUFFER_DESC_st {
    unsigned long long offset;
    unsigned long long size;
    unsigned int flags;
    unsigned int reserved[16];
} CUDA_EXTERNAL_MEMORY_BUFFER_DESC;

typedef enum CUexternalSemaphoreHandleType_enum {
    CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD = 1,
    CU_EXTERNAL_SEMAP_HANDLE_TYPE_OPAQUE_WIN32 = 2,
    CU_EXTERNAL_SEMAP_HANDLE_TYPE_OPAQUE_WIN32_KMT = 3,
    CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_TIMELINE_SEMAPHORE_FD = 10,
    CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_TIMELINE_SEMAPHORE_WIN32 = 11
} CUexternalSemaphoreHandleType;

typedef struct CUDA_EXTERNAL_SEMAPHORE_HANDLE_DESC_st {
    CUexternalSemaphoreHandleType type;
    union {
        int fd;
        struct {
            void *handle;
            const void *name;
        } win32;
        const void* NvSciSyncObj;
    } handle;
    unsigned int flags;
    unsigned int reserved[16];
} CUDA_EXTERNAL_SEMAPHORE_HANDLE_DESC;

typedef struct CUDA_EXTERNAL_SEMAPHORE_SIGNAL_PARAMS_st {
    struct {
        struct {
            unsigned long long value;
        } fence;
        unsigned int reserved[16];
    } params;
    unsigned int flags;
    unsigned int reserved[16];
} CUDA_EXTERNAL_SEMAPHORE_SIGNAL_PARAMS;

typedef CUDA_EXTERNAL_SEMAPHORE_SIGNAL_PARAMS CUDA_EXTERNAL_SEMAPHORE_WAIT_PARAMS;
#endif // CUDA_VERSION
#endif // __cuda_cuda_h__

// 引入位于项目的英伟达私有硬件编码 API
#include "../../external/nvidia/nvEncodeAPI.h"

namespace StuCanvas::Vulkan
{
    // CUDA 驱动核心函数指针类型定义
    typedef CUresult(CUDAAPI *PFN_cuInit)(unsigned int flags);
    typedef CUresult(CUDAAPI *PFN_cuDeviceGet)(CUdevice *device, int ordinal);
    typedef CUresult(CUDAAPI *PFN_cuCtxCreate)(CUcontext *pctx, unsigned int flags, CUdevice dev);
    typedef CUresult(CUDAAPI *PFN_cuCtxDestroy)(CUcontext ctx);
    typedef CUresult(CUDAAPI *PFN_cuCtxPushCurrent)(CUcontext ctx);
    typedef CUresult(CUDAAPI *PFN_cuCtxPopCurrent)(CUcontext *pctx);
    typedef CUresult(CUDAAPI *PFN_cuStreamCreate)(CUstream *phStream, unsigned int Flags);
    typedef CUresult(CUDAAPI *PFN_cuStreamDestroy)(CUstream hStream);
    typedef CUresult(CUDAAPI *PFN_cuMemFree)(CUdeviceptr devPtr);
    typedef CUresult(CUDAAPI *PFN_cuImportExternalMemory)(CUexternalMemory *extMem_out, const CUDA_EXTERNAL_MEMORY_HANDLE_DESC *memHandleDesc);
    typedef CUresult(CUDAAPI *PFN_cuDestroyExternalMemory)(CUexternalMemory extMem);
    typedef CUresult(CUDAAPI *PFN_cuExternalMemoryGetMappedBuffer)(CUdeviceptr *devPtr, CUexternalMemory extMem, const CUDA_EXTERNAL_MEMORY_BUFFER_DESC *bufferDesc);
    typedef CUresult(CUDAAPI *PFN_cuImportExternalSemaphore)(CUexternalSemaphore *extSem_out, const CUDA_EXTERNAL_SEMAPHORE_HANDLE_DESC *semHandleDesc);
    typedef CUresult(CUDAAPI *PFN_cuDestroyExternalSemaphore)(CUexternalSemaphore extSem);
    typedef CUresult(CUDAAPI *PFN_cuWaitExternalSemaphoresAsync)(const CUexternalSemaphore *extSemArray, const CUDA_EXTERNAL_SEMAPHORE_WAIT_PARAMS *paramsArray, unsigned int numExtSems, CUstream stream);
    typedef CUresult(CUDAAPI *PFN_cuSignalExternalSemaphoresAsync)(const CUexternalSemaphore *extSemArray, const CUDA_EXTERNAL_SEMAPHORE_SIGNAL_PARAMS *paramsArray, unsigned int numExtSems, CUstream stream);

    // NVENC 实例化接口函数指针类型定义
    typedef NVENCSTATUS(NVENCAPI *PFN_NvEncodeAPICreateInstance)(NV_ENCODE_API_FUNCTION_LIST*);

    /**
     * @brief CUDA 上下文作用域管理器 (RAII)
     */
    class CudaContextScope {
    public:
        CudaContextScope(CUcontext ctx, PFN_cuCtxPushCurrent push_fn, PFN_cuCtxPopCurrent pop_fn)
            : pop_fn_(pop_fn) {
            if (push_fn && ctx) {
                push_fn(ctx);
            }
        }
        ~CudaContextScope() {
            if (pop_fn_) {
                CUcontext dummy;
                pop_fn_(&dummy);
            }
        }
    private:
        PFN_cuCtxPopCurrent pop_fn_;
    };

    /**
     * @brief 编码配置与性能级别
     */
    enum class NvencCodec {
        H264,
        H265,
        AV1
    };

    struct NvencEncoderConfig {
        NvencCodec codec = NvencCodec::H264;
        uint32_t width = 2560;
        uint32_t height = 1600;
        uint32_t fps = 60;
        uint32_t bitrate_bps = 50000000;      // 默认 50 Mbps
        uint32_t max_bitrate_bps = 80000000;  // 默认 80 Mbps
        uint32_t gop_size = 60;
        uint32_t quality_preset = 7;          // 对应 NVENC 预设 P1 (1) 至 P7 (7)
    };

    /**
     * @brief 支持 Vulkan 外部同步与内存导出的高级 NVIDIA 视频编码器
     */
    class NvencCudaEncoder {
    public:
        struct CachedVulkanMemory {
            CUexternalMemory ext_mem = nullptr;
            CUdeviceptr dev_ptr = 0;
            NV_ENC_REGISTERED_PTR registered_handle = nullptr;
            NV_ENC_OUTPUT_PTR bitstream_buffer = nullptr;
            uint32_t width = 0;
            uint32_t height = 0;
            uint32_t pitch = 0;
        };

        NvencCudaEncoder(const NvencEncoderConfig& config) : config_(config) {
            loadCudaDriver();
            loadNvencAPI();

            CUresult cu_res = cuInit(0);
            if (cu_res != CUDA_SUCCESS) {
                throw std::runtime_error("NVENC/CUDA: cuInit failed.");
            }

            cu_res = cuDeviceGet(&cu_device_, 0);
            if (cu_res != CUDA_SUCCESS) {
                throw std::runtime_error("NVENC/CUDA: cuDeviceGet failed.");
            }

            cu_res = cuCtxCreate(&cuda_ctx_, 0, cu_device_);
            if (cu_res != CUDA_SUCCESS) {
                throw std::runtime_error("NVENC/CUDA: cuCtxCreate failed.");
            }

            CudaContextScope scope(cuda_ctx_, cuCtxPushCurrent, cuCtxPopCurrent);

            cu_res = cuStreamCreate(&cuda_stream_, 0);
            if (cu_res != CUDA_SUCCESS) {
                throw std::runtime_error("NVENC/CUDA: cuStreamCreate failed.");
            }

            // 初始化 NVENC 会话
            NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS session_params{};
            std::memset(&session_params, 0, sizeof(session_params));
            session_params.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
            session_params.deviceType = NV_ENC_DEVICE_TYPE_CUDA;
            session_params.device = (void*)cuda_ctx_;
            session_params.apiVersion = NVENCAPI_VERSION;

            NVENCSTATUS nv_status = nvenc_funcs_.nvEncOpenEncodeSessionEx(&session_params, &encoder_);
            if (nv_status != NV_ENC_SUCCESS) {
                throw std::runtime_error("NVENC/CUDA: nvEncOpenEncodeSessionEx failed.");
            }

            // 根据配置设定格式 GUID 和质量参数
            GUID codec_guid = GetCodecGUID(config_.codec);
            GUID preset_guid = GetPresetGUID(config_.quality_preset);

            NV_ENC_INITIALIZE_PARAMS init_params{};
            std::memset(&init_params, 0, sizeof(init_params));
            init_params.version = NV_ENC_INITIALIZE_PARAMS_VER;
            init_params.encodeGUID = codec_guid;
            init_params.presetGUID = preset_guid;
            init_params.encodeWidth = config_.width;
            init_params.encodeHeight = config_.height;
            init_params.darWidth = config_.width;
            init_params.darHeight = config_.height;
            init_params.frameRateNum = config_.fps;
            init_params.frameRateDen = 1;
            init_params.enablePTD = 1;
            init_params.tuningInfo = NV_ENC_TUNING_INFO_LOW_LATENCY;

            NV_ENC_PRESET_CONFIG preset_config{};
            std::memset(&preset_config, 0, sizeof(preset_config));
            preset_config.version = NV_ENC_PRESET_CONFIG_VER;
            preset_config.presetCfg.version = NV_ENC_CONFIG_VER;

            nv_status = nvenc_funcs_.nvEncGetEncodePresetConfigEx(
                encoder_, codec_guid, preset_guid, NV_ENC_TUNING_INFO_LOW_LATENCY, &preset_config);
            if (nv_status == NV_ENC_SUCCESS) {
                preset_config.presetCfg.gopLength = config_.gop_size;
                preset_config.presetCfg.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR;
                preset_config.presetCfg.rcParams.averageBitRate = config_.bitrate_bps;
                preset_config.presetCfg.rcParams.maxBitRate = config_.max_bitrate_bps;
                init_params.encodeConfig = &preset_config.presetCfg;
            }

            nv_status = nvenc_funcs_.nvEncInitializeEncoder(encoder_, &init_params);
            if (nv_status != NV_ENC_SUCCESS) {
                throw std::runtime_error("NVENC/CUDA: nvEncInitializeEncoder failed.");
            }

            nv_status = nvenc_funcs_.nvEncSetIOCudaStreams(encoder_, (NV_ENC_CUSTREAM_PTR)&cuda_stream_, (NV_ENC_CUSTREAM_PTR)&cuda_stream_);
            if (nv_status != NV_ENC_SUCCESS) {
                throw std::runtime_error("NVENC/CUDA: nvEncSetIOCudaStreams failed.");
            }
        }

        ~NvencCudaEncoder() {
            if (encoder_) {
                CudaContextScope scope(cuda_ctx_, cuCtxPushCurrent, cuCtxPopCurrent);

                for (auto& pair : cached_vulkan_memories_) {
                    auto& cached = pair.second;
                    if (cached.registered_handle) {
                        nvenc_funcs_.nvEncUnregisterResource(encoder_, cached.registered_handle);
                    }
                    if (cached.bitstream_buffer) {
                        nvenc_funcs_.nvEncDestroyBitstreamBuffer(encoder_, cached.bitstream_buffer);
                    }
                    if (cached.dev_ptr) {
                        cuMemFree(cached.dev_ptr);
                    }
                    if (cached.ext_mem) {
                        cuDestroyExternalMemory(cached.ext_mem);
                    }
                }
                cached_vulkan_memories_.clear();

                nvenc_funcs_.nvEncDestroyEncoder(encoder_);
                encoder_ = nullptr;
            }

            if (cuda_ctx_) {
                CudaContextScope scope(cuda_ctx_, cuCtxPushCurrent, cuCtxPopCurrent);
                if (cuda_stream_) {
                    cuStreamDestroy(cuda_stream_);
                    cuda_stream_ = nullptr;
                }
                cuCtxDestroy(cuda_ctx_);
                cuda_ctx_ = nullptr;
            }

            if (nvenc_lib_) FREE_LIBRARY(nvenc_lib_);
            if (cuda_lib_) FREE_LIBRARY(cuda_lib_);
        }

        // ====================================================================
        // 3. 跨平台信号量外部导入与等待机制
        // ====================================================================

#if defined(_WIN32)
        CUexternalSemaphore ImportSemaphore(void* win32_handle, bool is_timeline = false) {
            CudaContextScope scope(cuda_ctx_, cuCtxPushCurrent, cuCtxPopCurrent);

            CUDA_EXTERNAL_SEMAPHORE_HANDLE_DESC desc{};
            std::memset(&desc, 0, sizeof(desc));
            desc.type = is_timeline ? CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_TIMELINE_SEMAPHORE_WIN32
                                    : CU_EXTERNAL_SEMAP_HANDLE_TYPE_OPAQUE_WIN32;
            desc.handle.win32.handle = win32_handle;

            CUexternalSemaphore sem = nullptr;
            CUresult res = cuImportExternalSemaphore(&sem, &desc);
            if (res != CUDA_SUCCESS) {
                throw std::runtime_error("NVENC/CUDA: Failed to import Win32 shared semaphore.");
            }
            return sem;
        }
#else
        CUexternalSemaphore ImportSemaphore(int fd, bool is_timeline = false) {
            CudaContextScope scope(cuda_ctx_, cuCtxPushCurrent, cuCtxPopCurrent);

            CUDA_EXTERNAL_SEMAPHORE_HANDLE_DESC desc{};
            std::memset(&desc, 0, sizeof(desc));
            desc.type = is_timeline ? CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_TIMELINE_SEMAPHORE_FD
                                    : CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD;
            desc.handle.fd = fd;

            CUexternalSemaphore sem = nullptr;
            CUresult res = cuImportExternalSemaphore(&sem, &desc);
            if (res != CUDA_SUCCESS) {
                throw std::runtime_error("NVENC/CUDA: Failed to import Linux opaque FD semaphore.");
            }
            return sem;
        }
#endif

        void DestroySemaphore(CUexternalSemaphore sem) {
            if (sem) {
                CudaContextScope scope(cuda_ctx_, cuCtxPushCurrent, cuCtxPopCurrent);
                cuDestroyExternalSemaphore(sem);
            }
        }

        void WaitSemaphore(CUexternalSemaphore sem, uint64_t value = 0) {
            CudaContextScope scope(cuda_ctx_, cuCtxPushCurrent, cuCtxPopCurrent);

            CUDA_EXTERNAL_SEMAPHORE_WAIT_PARAMS wait_params{};
            std::memset(&wait_params, 0, sizeof(wait_params));
            wait_params.params.fence.value = value;
            wait_params.flags = 0;

            CUresult res = cuWaitExternalSemaphoresAsync(&sem, &wait_params, 1, cuda_stream_);
            if (res != CUDA_SUCCESS) {
                throw std::runtime_error("NVENC/CUDA: cuWaitExternalSemaphoresAsync failed.");
            }
        }

        void SignalSemaphore(CUexternalSemaphore sem, uint64_t value = 0) {
            CudaContextScope scope(cuda_ctx_, cuCtxPushCurrent, cuCtxPopCurrent);

            CUDA_EXTERNAL_SEMAPHORE_SIGNAL_PARAMS signal_params{};
            std::memset(&signal_params, 0, sizeof(signal_params));
            signal_params.params.fence.value = value;
            signal_params.flags = 0;

            CUresult res = cuSignalExternalSemaphoresAsync(&sem, &signal_params, 1, cuda_stream_);
            if (res != CUDA_SUCCESS) {
                throw std::runtime_error("NVENC/CUDA: cuSignalExternalSemaphoresAsync failed.");
            }
        }

        // ====================================================================
        // 4. Vulkan 物理分配内存绑定与图像编码
        // ====================================================================

        /**
         * @brief 编码导入的 Vulkan 物理帧，内部自动判断和处理显存的映射及持久化缓存
         */
        void EncodeVulkanFrame(
            VkDevice vk_device,
            VkImage vk_image,
            VkDeviceMemory vk_memory,
            uint64_t memory_size,
            uint32_t pitch,
            uint32_t frame_idx,
            std::vector<uint8_t>& out_bitstream,
            bool force_key_frame = false
        ) {
            CudaContextScope scope(cuda_ctx_, cuCtxPushCurrent, cuCtxPopCurrent);

            // A. 查询缓存。如果内存是首轮遇到，则对其进行 CUDA 导入与注册
            auto it = cached_vulkan_memories_.find(vk_memory);
            if (it == cached_vulkan_memories_.end()) {

                CUDA_EXTERNAL_MEMORY_HANDLE_DESC ext_desc{};
                std::memset(&ext_desc, 0, sizeof(ext_desc));

#ifdef _WIN32
                ext_desc.type = CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32;
                ext_desc.handle.win32.handle = GetVulkanMemoryHandle(vk_device, vk_memory);
#else
                ext_desc.type = CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD;
                ext_desc.handle.fd = GetVulkanMemoryHandle(vk_device, vk_memory);
#endif
                ext_desc.size = memory_size;

                CUexternalMemory ext_mem = nullptr;
                CUresult cu_res = cuImportExternalMemory(&ext_mem, &ext_desc);
                if (cu_res != CUDA_SUCCESS) {
                    throw std::runtime_error("NVENC/CUDA: cuImportExternalMemory failed.");
                }

                // 映射到统一的物理寻址指针
                CUDA_EXTERNAL_MEMORY_BUFFER_DESC buffer_desc{};
                std::memset(&buffer_desc, 0, sizeof(buffer_desc));
                buffer_desc.offset = 0;
                buffer_desc.size = memory_size;

                CUdeviceptr dev_ptr = 0;
                cu_res = cuExternalMemoryGetMappedBuffer(&dev_ptr, ext_mem, &buffer_desc);
                if (cu_res != CUDA_SUCCESS) {
                    cuDestroyExternalMemory(ext_mem);
                    throw std::runtime_error("NVENC/CUDA: cuExternalMemoryGetMappedBuffer failed.");
                }

                // 将指针注册到 NVENC 的缓冲接收通道
                NV_ENC_REGISTER_RESOURCE reg_params{};
                std::memset(&reg_params, 0, sizeof(reg_params));
                reg_params.version = NV_ENC_REGISTER_RESOURCE_VER;
                reg_params.resourceType = NV_ENC_INPUT_RESOURCE_TYPE_CUDADEVICEPTR;
                reg_params.resourceToRegister = (void*)dev_ptr;
                reg_params.width = config_.width;
                reg_params.height = config_.height;
                reg_params.pitch = pitch;
                reg_params.bufferFormat = NV_ENC_BUFFER_FORMAT_NV12; // NV12
                reg_params.bufferUsage = NV_ENC_INPUT_IMAGE;

                NVENCSTATUS status = nvenc_funcs_.nvEncRegisterResource(encoder_, &reg_params);
                if (status != NV_ENC_SUCCESS) {
                    cuMemFree(dev_ptr);
                    cuDestroyExternalMemory(ext_mem);
                    throw std::runtime_error("NVENC: nvEncRegisterResource failed for Vulkan frame.");
                }

                // 创建该槽位关联的位流输出资源
                NV_ENC_CREATE_BITSTREAM_BUFFER bitstream_params{};
                std::memset(&bitstream_params, 0, sizeof(bitstream_params));
                bitstream_params.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;

                status = nvenc_funcs_.nvEncCreateBitstreamBuffer(encoder_, &bitstream_params);
                if (status != NV_ENC_SUCCESS) {
                    nvenc_funcs_.nvEncUnregisterResource(encoder_, reg_params.registeredResource);
                    cuMemFree(dev_ptr);
                    cuDestroyExternalMemory(ext_mem);
                    throw std::runtime_error("NVENC: Failed to allocate output bitstream buffer.");
                }

                CachedVulkanMemory cached_entry{};
                cached_entry.ext_mem = ext_mem;
                cached_entry.dev_ptr = dev_ptr;
                cached_entry.registered_handle = reg_params.registeredResource;
                cached_entry.bitstream_buffer = bitstream_params.bitstreamBuffer;
                cached_entry.width = config_.width;
                cached_entry.height = config_.height;
                cached_entry.pitch = pitch;

                cached_vulkan_memories_[vk_memory] = cached_entry;
                it = cached_vulkan_memories_.find(vk_memory);
            }

            auto& cached = it->second;

            // 1. 映射输入物理地址
            NV_ENC_MAP_INPUT_RESOURCE map_params{};
            std::memset(&map_params, 0, sizeof(map_params));
            map_params.version = NV_ENC_MAP_INPUT_RESOURCE_VER;
            map_params.registeredResource = cached.registered_handle;

            NVENCSTATUS status = nvenc_funcs_.nvEncMapInputResource(encoder_, &map_params);
            if (status != NV_ENC_SUCCESS) {
                throw std::runtime_error("NVENC: Resource mapping failed.");
            }

            // 2. 装配帧控制块并发送异步编码
            NV_ENC_PIC_PARAMS pic_params{};
            std::memset(&pic_params, 0, sizeof(pic_params));
            pic_params.version = NV_ENC_PIC_PARAMS_VER;
            pic_params.inputWidth = cached.width;
            pic_params.inputHeight = cached.height;
            pic_params.inputPitch = cached.pitch;
            pic_params.inputBuffer = map_params.mappedResource;
            pic_params.outputBitstream = cached.bitstream_buffer;
            pic_params.bufferFmt = NV_ENC_BUFFER_FORMAT_NV12;
            pic_params.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
            pic_params.frameIdx = frame_idx;

            if (force_key_frame) {
                pic_params.encodePicFlags |= NV_ENC_PIC_FLAG_FORCEIDR;
            }

            status = nvenc_funcs_.nvEncEncodePicture(encoder_, &pic_params);
            if (status != NV_ENC_SUCCESS && status != NV_ENC_ERR_NEED_MORE_INPUT) {
                nvenc_funcs_.nvEncUnmapInputResource(encoder_, map_params.mappedResource);
                throw std::runtime_error("NVENC: nvEncEncodePicture failed.");
            }

            // 3. 注销映射
            status = nvenc_funcs_.nvEncUnmapInputResource(encoder_, map_params.mappedResource);
            if (status != NV_ENC_SUCCESS) {
                throw std::runtime_error("NVENC: Resource unmapping failed.");
            }

            // 4. 获取已经完成的位流字节
            if (status == NV_ENC_SUCCESS) {
                NV_ENC_LOCK_BITSTREAM lock_params{};
                std::memset(&lock_params, 0, sizeof(lock_params));
                lock_params.version = NV_ENC_LOCK_BITSTREAM_VER;
                lock_params.outputBitstream = cached.bitstream_buffer;
                lock_params.doNotWait = 0; // 阻塞等待 CUDA 流运行完毕

                status = nvenc_funcs_.nvEncLockBitstream(encoder_, &lock_params);
                if (status == NV_ENC_SUCCESS) {
                    uint32_t size = lock_params.bitstreamSizeInBytes;
                    if (size > 0 && lock_params.bitstreamBufferPtr) {
                        out_bitstream.resize(size);
                        std::memcpy(out_bitstream.data(), lock_params.bitstreamBufferPtr, size);
                    }
                    nvenc_funcs_.nvEncUnlockBitstream(encoder_, cached.bitstream_buffer);
                }
            }
        }

    private:
        inline GUID GetPresetGUID(uint32_t preset_val) {
            switch (preset_val) {
                case 1: return NV_ENC_PRESET_P1_GUID;
                case 2: return NV_ENC_PRESET_P2_GUID;
                case 3: return NV_ENC_PRESET_P3_GUID;
                case 4: return NV_ENC_PRESET_P4_GUID;
                case 5: return NV_ENC_PRESET_P5_GUID;
                case 6: return NV_ENC_PRESET_P6_GUID;
                case 7: return NV_ENC_PRESET_P7_GUID;
                default: return NV_ENC_PRESET_P4_GUID;
            }
        }

        inline GUID GetCodecGUID(NvencCodec codec) {
            switch (codec) {
                case NvencCodec::H264: return NV_ENC_CODEC_H264_GUID;
                case NvencCodec::H265: return NV_ENC_CODEC_HEVC_GUID;
                case NvencCodec::AV1:  return NV_ENC_CODEC_AV1_GUID;
            }
            return NV_ENC_CODEC_H264_GUID;
        }

#ifndef _WIN32
        int GetVulkanMemoryHandle(VkDevice vk_device, VkDeviceMemory vk_memory) {
            VkMemoryGetFdInfoKHR fd_info{};
            fd_info.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
            fd_info.pNext = nullptr;
            fd_info.memory = vk_memory;
            fd_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

            auto func = (PFN_vkGetMemoryFdKHR)vkGetDeviceProcAddr(vk_device, "vkGetMemoryFdKHR");
            if (!func) {
                throw std::runtime_error("NVENC/Vulkan: Failed to locate function vkGetMemoryFdKHR.");
            }

            int fd = -1;
            VkResult r = func(vk_device, &fd_info, &fd);
            if (r != VK_SUCCESS) {
                throw std::runtime_error("NVENC/Vulkan: vkGetMemoryFdKHR execution failed.");
            }
            return fd;
        }
#else
        void* GetVulkanMemoryHandle(VkDevice vk_device, VkDeviceMemory vk_memory) {
            VkMemoryGetWin32HandleInfoKHR win32_info{};
            win32_info.sType = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR;
            win32_info.pNext = nullptr;
            win32_info.memory = vk_memory;
            win32_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;

            auto func = (PFN_vkGetMemoryWin32HandleKHR)vkGetDeviceProcAddr(vk_device, "vkGetMemoryWin32HandleKHR");
            if (!func) {
                throw std::runtime_error("NVENC/Vulkan: Failed to locate function vkGetMemoryWin32HandleKHR.");
            }

            void* handle = nullptr;
            VkResult r = func(vk_device, &win32_info, &handle);
            if (r != VK_SUCCESS) {
                throw std::runtime_error("NVENC/Vulkan: vkGetMemoryWin32HandleKHR execution failed.");
            }
            return handle;
        }
#endif

        void loadCudaDriver() {
#ifdef _WIN32
            cuda_lib_ = LOAD_LIBRARY("nvcuda.dll");
#else
            cuda_lib_ = LOAD_LIBRARY("libcuda.so.1");
            if (!cuda_lib_) cuda_lib_ = LOAD_LIBRARY("libcuda.so");
#endif
            if (!cuda_lib_) throw std::runtime_error("NVENC: Failed to load CUDA driver library.");

            auto load_sym = [this](const char* name) -> void* {
                void* sym = GET_SYMBOL(cuda_lib_, name);
                if (!sym) throw std::runtime_error(std::string("NVENC: Failed to load CUDA symbol: ") + name);
                return sym;
            };

            cuInit = (PFN_cuInit)load_sym("cuInit");
            cuDeviceGet = (PFN_cuDeviceGet)load_sym("cuDeviceGet");

            cuCtxCreate = (PFN_cuCtxCreate)GET_SYMBOL(cuda_lib_, "cuCtxCreate_v2");
            if (!cuCtxCreate) cuCtxCreate = (PFN_cuCtxCreate)load_sym("cuCtxCreate");

            cuCtxDestroy = (PFN_cuCtxDestroy)GET_SYMBOL(cuda_lib_, "cuCtxDestroy_v2");
            if (!cuCtxDestroy) cuCtxDestroy = (PFN_cuCtxDestroy)load_sym("cuCtxDestroy");

            cuCtxPushCurrent = (PFN_cuCtxPushCurrent)GET_SYMBOL(cuda_lib_, "cuCtxPushCurrent_v2");
            if (!cuCtxPushCurrent) cuCtxPushCurrent = (PFN_cuCtxPushCurrent)load_sym("cuCtxPushCurrent");

            cuCtxPopCurrent = (PFN_cuCtxPopCurrent)GET_SYMBOL(cuda_lib_, "cuCtxPopCurrent_v2");
            if (!cuCtxPopCurrent) cuCtxPopCurrent = (PFN_cuCtxPopCurrent)load_sym("cuCtxPopCurrent");

            cuStreamCreate = (PFN_cuStreamCreate)load_sym("cuStreamCreate");
            cuStreamDestroy = (PFN_cuStreamDestroy)load_sym("cuStreamDestroy");

            cuMemFree = (PFN_cuMemFree)GET_SYMBOL(cuda_lib_, "cuMemFree_v2");
            if (!cuMemFree) cuMemFree = (PFN_cuMemFree)load_sym("cuMemFree");

            cuImportExternalMemory = (PFN_cuImportExternalMemory)load_sym("cuImportExternalMemory");
            cuDestroyExternalMemory = (PFN_cuDestroyExternalMemory)load_sym("cuDestroyExternalMemory");
            cuExternalMemoryGetMappedBuffer = (PFN_cuExternalMemoryGetMappedBuffer)load_sym("cuExternalMemoryGetMappedBuffer");

            cuImportExternalSemaphore = (PFN_cuImportExternalSemaphore)load_sym("cuImportExternalSemaphore");
            cuDestroyExternalSemaphore = (PFN_cuDestroyExternalSemaphore)load_sym("cuDestroyExternalSemaphore");
            cuWaitExternalSemaphoresAsync = (PFN_cuWaitExternalSemaphoresAsync)load_sym("cuWaitExternalSemaphoresAsync");
            cuSignalExternalSemaphoresAsync = (PFN_cuSignalExternalSemaphoresAsync)load_sym("cuSignalExternalSemaphoresAsync");
        }

        void loadNvencAPI() {
#ifdef _WIN32
            nvenc_lib_ = LOAD_LIBRARY("nvEncodeAPI64.dll");
#else
            nvenc_lib_ = LOAD_LIBRARY("libnvidia-encode.so.1");
            if (!nvenc_lib_) nvenc_lib_ = LOAD_LIBRARY("libnvidia-encode.so");
#endif
            if (!nvenc_lib_) throw std::runtime_error("NVENC: Failed to load NVENC runtime library.");

            PFN_NvEncodeAPICreateInstance pfn_create =
                (PFN_NvEncodeAPICreateInstance)GET_SYMBOL(nvenc_lib_, "NvEncodeAPICreateInstance");
            if (!pfn_create) throw std::runtime_error("NVENC: NvEncodeAPICreateInstance entry point missing.");

            std::memset(&nvenc_funcs_, 0, sizeof(nvenc_funcs_));
            nvenc_funcs_.version = NV_ENCODE_API_FUNCTION_LIST_VER;

            NVENCSTATUS status = pfn_create(&nvenc_funcs_);
            if (status != NV_ENC_SUCCESS) {
                throw std::runtime_error("NVENC: Failed to instantiate function pointers from NVENC driver.");
            }
        }

        LibraryHandle cuda_lib_ = nullptr;
        LibraryHandle nvenc_lib_ = nullptr;

        CUdevice cu_device_{};
        CUcontext cuda_ctx_ = nullptr;
        CUstream cuda_stream_ = nullptr;

        void* encoder_ = nullptr;
        NV_ENCODE_API_FUNCTION_LIST nvenc_funcs_{};

        NvencEncoderConfig config_;

        // Vulkan 物理物理显存导入缓冲哈希缓存
        std::unordered_map<VkDeviceMemory, CachedVulkanMemory> cached_vulkan_memories_;

        // CUDA 接口函数指针
        PFN_cuInit cuInit = nullptr;
        PFN_cuDeviceGet cuDeviceGet = nullptr;
        PFN_cuCtxCreate cuCtxCreate = nullptr;
        PFN_cuCtxDestroy cuCtxDestroy = nullptr;
        PFN_cuCtxPushCurrent cuCtxPushCurrent = nullptr;
        PFN_cuCtxPopCurrent cuCtxPopCurrent = nullptr;
        PFN_cuStreamCreate cuStreamCreate = nullptr;
        PFN_cuStreamDestroy cuStreamDestroy = nullptr;
        PFN_cuMemFree cuMemFree = nullptr;

        PFN_cuImportExternalMemory cuImportExternalMemory = nullptr;
        PFN_cuDestroyExternalMemory cuDestroyExternalMemory = nullptr;
        PFN_cuExternalMemoryGetMappedBuffer cuExternalMemoryGetMappedBuffer = nullptr;

        PFN_cuImportExternalSemaphore cuImportExternalSemaphore = nullptr;
        PFN_cuDestroyExternalSemaphore cuDestroyExternalSemaphore = nullptr;
        PFN_cuWaitExternalSemaphoresAsync cuWaitExternalSemaphoresAsync = nullptr;
        PFN_cuSignalExternalSemaphoresAsync cuSignalExternalSemaphoresAsync = nullptr;
    };
} // namespace StuCanvas::Vulkan