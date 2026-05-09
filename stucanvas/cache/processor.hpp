#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <type_traits>
#include <cstdint>
#include <cstring>
#include <functional>

namespace StuCanvas::cache {

    // 永恒不变的魔数，标识文件完整性
    inline constexpr uint64_t STU_MAGIC_FOOTER = 0x5354554341434845;

    // ---- 检测类型是否具备自定义序列化接口 ----
    namespace detail {
        template <typename T, typename = void>
        struct has_serialize : std::false_type {};

        template <typename T>
        struct has_serialize<T, std::void_t<
            decltype(std::declval<const T&>().SerializedSize()),
            decltype(std::declval<const T&>().Serialize(std::declval<void*>())),
            decltype(std::declval<T&>().Deserialize(std::declval<const void*>(), std::declval<size_t>()))
        >> : std::true_type {};
    } // namespace detail

    class BlockProcessor {
        // 单条缓存记录
        struct Record {
            void* obj_ptr   = nullptr;
            bool  is_pod    = false;
            size_t pod_size = 0;                       // 仅 POD 类型有效 ( == sizeof(T) )

            // 非 POD 类型使用的类型擦除函数
            std::function<std::vector<uint8_t>()>       serialize_func;   // 返回序列化数据
            std::function<void(const void*, size_t)>    deserialize_func; // 从数据反序列化
        };

        std::string          label_;
        size_t               hash_;
        std::vector<Record>  records_;

    public:
        BlockProcessor(const std::string& label, size_t hash)
            : label_(label), hash_(hash) {}

        // 绑定变量（自动识别 POD 或 自定义序列化对象）
        template <typename T>
        void Bind(T& var) {
            if constexpr (std::is_trivial_v<T> && std::is_standard_layout_v<T>) {
                records_.push_back({&var, true, sizeof(T), nullptr, nullptr});
            } else {
                // 强制要求自定义序列化接口，否则编译报错
                static_assert(detail::has_serialize<T>::value,
                    "Non-POD type must provide: SerializedSize(), Serialize(void*), "
                    "Deserialize(const void*, size_t)");

                records_.push_back({
                    &var,
                    false,
                    0, // pod_size 无效
                    // 序列化：计算大小 -> 分配缓冲区 -> 调用 Serialize
                    [&var]() -> std::vector<uint8_t> {
                        size_t sz = var.SerializedSize();
                        std::vector<uint8_t> buf(sz);
                        var.Serialize(buf.data());
                        return buf;
                    },
                    // 反序列化：调用对象的 Deserialize
                    [&var](const void* data, size_t size) {
                        var.Deserialize(data, size);
                    }
                });
            }
        }

        // 批量绑定
        template <typename... Ts>
        void BindAll(Ts&... vars) {
            (Bind(vars), ...);
        }

        /**
         * @brief 尝试从缓存文件加载数据
         * @return 加载成功返回 true，否则 false（缓存未命中或校验失败）
         */
        bool TryLoad() {
            namespace fs = std::filesystem;
            std::string path = label_ + ".stucache";

            if (!fs::exists(path)) return false;

            std::ifstream is(path, std::ios::binary | std::ios::ate);
            size_t file_size = is.tellg();
            if (file_size < sizeof(size_t) + sizeof(uint64_t)) return false;

            // 读取尾部的 hash 与 魔数
            is.seekg(-static_cast<int>(sizeof(size_t) + sizeof(uint64_t)), std::ios::end);
            size_t stored_hash = 0;
            uint64_t magic = 0;
            is.read(reinterpret_cast<char*>(&stored_hash), sizeof(size_t));
            is.read(reinterpret_cast<char*>(&magic), sizeof(uint64_t));

            if (magic != STU_MAGIC_FOOTER || stored_hash != hash_) return false;

            // 回到文件开头，按记录数量循环读取
            is.seekg(0, std::ios::beg);
            size_t data_end = file_size - sizeof(size_t) - sizeof(uint64_t); // 数据区的末尾偏移

            std::vector<uint8_t> temp_buf;
            for (auto& rec : records_) {
                // 读取长度前缀
                std::streamoff current_pos = is.tellg();
                if (current_pos + static_cast<std::streamoff>(sizeof(uint32_t)) > static_cast<std::streamoff>(data_end)) {
                    return false; // 数据不完整
                }
                uint32_t size = 0;
                is.read(reinterpret_cast<char*>(&size), sizeof(size));

                if (size == 0) continue; // 防御，但理论上不应该有零长度

                // 检查数据体是否在文件范围内
                current_pos = is.tellg();
                if (current_pos + static_cast<std::streamoff>(size) > static_cast<std::streamoff>(data_end)){
                    return false;
                }

                if (rec.is_pod) {
                    // POD 类型：直接读入对象内存（需确保 size == sizeof(T)）
                    if (size != rec.pod_size) return false;
                    is.read(reinterpret_cast<char*>(rec.obj_ptr), size);
                } else {
                    // 非 POD 类型：读入临时缓冲区，再反序列化
                    temp_buf.resize(size);
                    is.read(reinterpret_cast<char*>(temp_buf.data()), size);
                    rec.deserialize_func(temp_buf.data(), size);
                }
            }
            return true;
        }

        /**
         * @brief 将当前对象数据写入缓存文件（先写临时文件再原子替换）
         */
        void Save() {
            namespace fs = std::filesystem;
            std::string final_path = label_ + ".stucache";
            std::string tmp_path   = final_path + ".tmp";

            {
                std::ofstream os(tmp_path, std::ios::binary | std::ios::trunc);

                for (auto& rec : records_) {
                    if (rec.is_pod) {
                        // POD 类型：写入 sizeof(T) 和内存数据
                        uint32_t size = static_cast<uint32_t>(rec.pod_size);
                        os.write(reinterpret_cast<const char*>(&size), sizeof(size));
                        os.write(reinterpret_cast<const char*>(rec.obj_ptr), size);
                    } else {
                        // 非 POD 类型：调用序列化函数获取数据
                        auto data = rec.serialize_func();
                        uint32_t size = static_cast<uint32_t>(data.size());
                        os.write(reinterpret_cast<const char*>(&size), sizeof(size));
                        os.write(reinterpret_cast<const char*>(data.data()), size);
                    }
                }

                // 写入 hash 和 魔数
                os.write(reinterpret_cast<const char*>(&hash_), sizeof(size_t));
                os.write(reinterpret_cast<const char*>(&STU_MAGIC_FOOTER), sizeof(uint64_t));
                os.flush();
            }

            // 原子化重命名
            std::error_code ec;
            fs::rename(tmp_path, final_path, ec);
            if (ec) {
                fs::remove(tmp_path, ec);
            }
        }
    };

} // namespace StuCanvas::cache