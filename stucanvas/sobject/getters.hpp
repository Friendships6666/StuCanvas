#pragma once
#include "graph.hpp"

namespace StuCanvas
{
    template <typename T>
    const SObjectData<T>& SObjectGraph<T>::GetResultData(const SObject<T>* node_ptr)
    {
        if (!node_ptr) throw std::invalid_argument("Null pointer");
        return node_ptr->data;
    }
}
