/***************************************************************************
* Copyright (c) 2026 StuCanvas Team                                        *
*                                                                          *
* Distributed under the terms of the MIT License.                          *
***************************************************************************/

#pragma once

#include <vector>
#include <manifold/manifold.h>
#include "../types/mesh.hpp"

namespace StuCanvas::Reconstruction
{
    /**
     * @brief 基于 Manifold 库的高性能 3D 布尔运算引擎
     *
     * 支持对两个 Mesh3D 对象进行并集（Union）、差集（Difference）和交集（Intersection）运算。
     * 自动处理内部数据转换，并保证输出结果为严格流形。
     */
    template <typename T = double>
    class BooleanEngine
    {
    public:
        /**
         * @brief 计算并集 (A + B)
         */
        static Mesh3D<T> Union(const Mesh3D<T>& meshA, const Mesh3D<T>& meshB)
        {
            return PerformOp(meshA, meshB, OpType::Add);
        }

        /**
         * @brief 计算差集 (A - B)
         */
        static Mesh3D<T> Difference(const Mesh3D<T>& meshA, const Mesh3D<T>& meshB)
        {
            return PerformOp(meshA, meshB, OpType::Subtract);
        }

        /**
         * @brief 计算交集 (A ^ B)
         */
        static Mesh3D<T> Intersection(const Mesh3D<T>& meshA, const Mesh3D<T>& meshB)
        {
            return PerformOp(meshA, meshB, OpType::Intersect);
        }

    private:
        enum class OpType { Add, Subtract, Intersect };

        /**
         * @brief 将 StuCanvas::Mesh3D 转换为 manifold::Manifold
         */
        static manifold::Manifold ToManifold(const Mesh3D<T>& mesh)
        {
            if (mesh.vertices.empty()) return manifold::Manifold();

            manifold::MeshGL64 mgl;
            mgl.numProp = 3; // 仅包含 x, y, z
            mgl.vertProperties.reserve(mesh.vertices.size() * 3);

            for (const auto& v : mesh.vertices) {
                mgl.vertProperties.push_back(static_cast<double>(v.position.x));
                mgl.vertProperties.push_back(static_cast<double>(v.position.y));
                mgl.vertProperties.push_back(static_cast<double>(v.position.z));
            }

            mgl.triVerts.reserve(mesh.indices.size());
            for (auto idx : mesh.indices) {
                mgl.triVerts.push_back(static_cast<uint64_t>(idx));
            }

            // 直接构造，此时 Manifold 内部会进行拓扑检查
            return manifold::Manifold(mgl);
        }

        /**
         * @brief 将 manifold::Manifold 转换回 StuCanvas::Mesh3D
         */
        static Mesh3D<T> FromManifold(const manifold::Manifold& m)
        {
            Mesh3D<T> result;
            if (m.IsEmpty()) return result;

            // 获取双精度网格数据
            manifold::MeshGL64 mgl = m.GetMeshGL64();

            result.vertices.reserve(mgl.NumVert());
            for (size_t i = 0; i < mgl.NumVert(); ++i) {
                result.vertices.emplace_back(Point3D<T>{
                    static_cast<T>(mgl.vertProperties[i * 3 + 0]),
                    static_cast<T>(mgl.vertProperties[i * 3 + 1]),
                    static_cast<T>(mgl.vertProperties[i * 3 + 2])
                });
            }

            result.indices.reserve(mgl.triVerts.size());
            for (auto idx : mgl.triVerts) {
                result.indices.push_back(static_cast<uint32_t>(idx));
            }

            return result;
        }

        /**
         * @brief 通用布尔运算执行器
         */
        static Mesh3D<T> PerformOp(const Mesh3D<T>& a, const Mesh3D<T>& b, OpType op)
        {
            auto maniA = ToManifold(a);
            auto maniB = ToManifold(b);

            manifold::Manifold result;
            switch (op) {
                case OpType::Add:       result = maniA + maniB; break;
                case OpType::Subtract:  result = maniA - maniB; break;
                case OpType::Intersect: result = maniA ^ maniB; break;
            }

            // 如果结果状态不正常（例如因浮点异常导致非流形），返回空网格
            if (result.Status() != manifold::Manifold::Error::NoError) {
                return Mesh3D<T>();
            }

            return FromManifold(result);
        }
    };
}