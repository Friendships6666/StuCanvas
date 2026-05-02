#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <type_traits>

namespace StuCanvas::cache {

    // 定义一个永恒不变的魔数，用于标识文件完整性
    // "STUCACHE" 的 ASCII 码
    inline constexpr uint64_t STU_MAGIC_FOOTER = 0x5354554341434845;

    struct CacheEntry {
        void* addr;
        size_t size;
    };

    class BlockProcessor {
        std::string label;
        size_t current_hash;
        std::vector<CacheEntry> entries;

    public:
        BlockProcessor(const std::string& l, size_t h)
            : label(l), current_hash(h) {}

        template <typename T>
        void Bind(T& var) {
            static_assert(std::is_standard_layout_v<T> && std::is_trivial_v<T>,
                "StuCanvas Error: Only POD types allowed.");
            entries.push_back({&var, sizeof(T)});
        }

        template <typename... Ts>
        void BindAll(Ts&... vars) { (Bind(vars), ...); }

        /**
         * @brief 增强版校验：必须 Hash 匹配 且 结尾魔数正确
         */
        bool TryLoad() {
            namespace fs = std::filesystem;
            std::string path = label + ".stucache"; // 合并为单文件

            if (!fs::exists(path)) return false;

            std::ifstream is(path, std::ios::binary | std::ios::ate); // ate: 打开即跳到文件末尾
            size_t file_size = is.tellg();

            // 基础检查：文件至少要能容纳 [数据] + [Hash] + [Magic]
            size_t total_data_size = 0;
            for (auto& e : entries) total_data_size += e.size;
            if (file_size < total_data_size + sizeof(size_t) + sizeof(uint64_t)) return false;

            // 1. 校验结尾魔数和 Hash
            // 定位到末尾倒数 16 字节
            is.seekg(-(int)(sizeof(size_t) + sizeof(uint64_t)), std::ios::end);
            size_t stored_hash = 0;
            uint64_t magic = 0;
            is.read(reinterpret_cast<char*>(&stored_hash), sizeof(size_t));
            is.read(reinterpret_cast<char*>(&magic), sizeof(uint64_t));

            if (magic != STU_MAGIC_FOOTER || stored_hash != current_hash) return false;

            // 2. 只有校验通过，才读取数据
            is.seekg(0, std::ios::beg);
            for (auto& e : entries) {
                is.read(reinterpret_cast<char*>(e.addr), e.size);
            }
            return true;
        }

        /**
         * @brief 原子化保存：先写 .tmp，写完再 rename
         */
        void Save() {
            namespace fs = std::filesystem;
            std::string final_path = label + ".stucache";
            std::string tmp_path = final_path + ".tmp";

            {
                std::ofstream os(tmp_path, std::ios::binary | std::ios::trunc);

                // 1. 写入数据主体
                for (auto& e : entries) {
                    os.write(reinterpret_cast<const char*>(e.addr), e.size);
                }

                // 2. 写入元数据和魔数
                // 只有写到这一行，说明前面的数据已经完整落盘
                os.write(reinterpret_cast<const char*>(&current_hash), sizeof(size_t));
                os.write(reinterpret_cast<const char*>(&STU_MAGIC_FOOTER), sizeof(uint64_t));

                os.flush();
                // ofstream 在此处作用域结束自动 Close
            }

            // 3. 核心黑魔法：原子重命名
            // 在 POSIX (Linux/macOS) 和 modern Windows 上，rename 操作是原子的。
            // 哪怕此时蓝屏，系统要么保留旧文件，要么生成完整的新文件，绝不会出现“半个文件”。
            std::error_code ec;
            fs::rename(tmp_path, final_path, ec);
            if (ec) {
                // 如果 rename 失败，尝试删除残余 tmp
                fs::remove(tmp_path, ec);
            }
        }
    };
}