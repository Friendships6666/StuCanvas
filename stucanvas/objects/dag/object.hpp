#pragma once
#include <bitset>
#include <cstdint>
#include <string>

#include "assets.hpp"
#include "compact_string.hpp"
#include "data.hpp"
#include "flex_vector.hpp"
#include "node_types.hpp"
#include "instance.hpp"
#include "tiny_vector.hpp"

namespace StuCanvas
{

    enum class NodeProperty : uint64_t
    {
        Solved
    };

    struct DAGraph;

    struct DAGObject
    {
        NodeType type;
        std::bitset< 64 > flag;
        NodeData data;
        utils::FlexVector<> assets;
        utils::TinyVector< DAGObject* > parents;
        utils::TinyVector< DAGObject* > children;
        utils::CompactString name;
        uint32_t id;
        DAGraph* graph;
        utils::TinyVector< DAGObjectInstance*> instances;
    };


}   // namespace StuCanvas
