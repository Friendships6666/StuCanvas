// --- 文件路径: include/CAS/JsonAdapter.h ---

#ifndef JSON_ADAPTER_H
#define JSON_ADAPTER_H

#include "../../../pch.h"

namespace CAS::GraphicSimplify {
    struct Expression;
}

namespace CAS::JsonAdapter {

    std::shared_ptr<CAS::GraphicSimplify::Expression> parse_json_to_ast_simdjson(const std::string& json_string);
    std::string ast_to_json_string(const std::shared_ptr<CAS::GraphicSimplify::Expression>& ast);

} // namespace CAS::JsonAdapter

#endif // JSON_ADAPTER_H