#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <type_traits>

namespace StuCanvas::cache {

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

        // 单个绑定逻辑
        template <typename T>
        void Bind(T& var) {
            static_assert(std::is_standard_layout_v<T> && std::is_trivial_v<T>,
                "StuCanvas Error: Only POD types (trivial + standard layout) can be cached in STU_BLOCK.");
            entries.push_back({&var, sizeof(T)});
        }

        // --- 新增：变长绑定入口 ---
        template <typename... Ts>
        void BindAll(Ts&... vars) {
            (Bind(vars), ...); // C++17 折叠表达式
        }

        bool TryLoad() {
            namespace fs = std::filesystem;
            std::string meta_p = label + ".meta";
            std::string data_p = label + ".bin";

            if (!fs::exists(meta_p) || !fs::exists(data_p)) return false;

            std::ifstream mf(meta_p, std::ios::binary);
            size_t stored_hash = 0;
            mf.read(reinterpret_cast<char*>(&stored_hash), sizeof(size_t));
            if (stored_hash != current_hash) return false;

            std::ifstream df(data_p, std::ios::binary);
            for (auto& e : entries) {
                df.read(reinterpret_cast<char*>(e.addr), e.size);
            }
            return true;
        }

        void Save() {
            std::ofstream odf(label + ".bin", std::ios::binary);
            for (auto& e : entries) {
                odf.write(reinterpret_cast<const char*>(e.addr), e.size);
            }
            std::ofstream omf(label + ".meta", std::ios::binary);
            omf.write(reinterpret_cast<const char*>(&current_hash), sizeof(size_t));
        }
    };
}