#pragma once
#include "graph.hpp"

namespace StuCanvas
{
    template <typename T>
    const SObjectData<T>& SObjectGraph<T>::GetResultData(const SObject<T>* node_ptr)
    {
        if (!node_ptr) throw std::invalid_argument("Null pointer");

        if (!node_ptr->has_mask(NodeMask::QUERIED))
        {
            node_ptr->set_mask(NodeMask::QUERIED);
            curr_frame_stats.query_count++;
        }

        return node_ptr->data;
    }
}
