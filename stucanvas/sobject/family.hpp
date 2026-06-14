#pragma once
#include <eigen3/Eigen/Dense>
#include <stdexcept>
namespace StuCanvas
{

    template <typename T>
    struct AABB3D
    {
        // 初始值：将最小边界设为无穷大，最大边界设为负无穷大，以便于用 cwiseMin/cwiseMax 自动扩张
        Eigen::Matrix<T, 3, 1> min = Eigen::Matrix<T, 3, 1>::Constant(std::numeric_limits<T>::max());
        Eigen::Matrix<T, 3, 1> max = Eigen::Matrix<T, 3, 1>::Constant(std::numeric_limits<T>::lowest());

        // 计算并获取包围盒的绝对几何中心（重心）
        Eigen::Matrix<T, 3, 1> GetCenter() const noexcept
        {
            return (min + max) * static_cast<T>(0.5);
        }
    };



    template <typename T>
    struct SObject;

    template <typename T>
    struct SObjectGraph;

    template <typename T>
    class SObjectFamily
    {
    public:
        // 构造：传入图谱和任意一个起始探测特征节点
        SObjectFamily(SObjectGraph<T>& graph, const SObject<T>* start_node)
            : m_graph(&graph), m_start_node(start_node)
        {
            refresh(); // 初始建立时，自动爬取并填充整个家族成员
        }

        // 刷新自身家族成员列表（当图谱拓扑发生 modifyParents 改变时，由图谱统一触发）
        void refresh()
        {
            if (m_graph && m_start_node)
            {
                // 调用图谱的 getFamily 算法，一瞬间获取该节点牵连的所有父子节点指针
                m_members = m_graph->getFamily(m_start_node);
            }
            else
            {
                m_members.clear();
            }
        }

        // 核心：自适应获取整块“铁板模型”合并后的局部包围盒
        AABB3D<T> GetLocalAABB() const
        {
            AABB3D<T> merged_box;
            bool has_valid_bounds = false;

            // 遍历整个家族里的所有成员，只对拥有 get_local_aabb 虚函数的可渲染几何体进行合并 [3.2.1]
            for (const auto* node : m_members)
            {
                if (node && node->vptr && node->vptr->get_local_aabb)
                {
                    Eigen::Matrix<T, 3, 1> local_min, local_max;
                    node->vptr->get_local_aabb(*node, local_min, local_max);

                    // 动态收缩扩张包围盒
                    merged_box.min = merged_box.min.cwiseMin(local_min);
                    merged_box.max = merged_box.max.cwiseMax(local_max);
                    has_valid_bounds = true;
                }
            }

            if (!has_valid_bounds)
            {
                merged_box.min.setZero();
                merged_box.max.setZero();
            }
            return merged_box;
        }

        const std::vector<SObject<T>*>& GetMembers() const noexcept { return m_members; }
        const SObject<T>* GetStartNode() const noexcept { return m_start_node; }

    private:
        SObjectGraph<T>* m_graph = nullptr;
        const SObject<T>* m_start_node = nullptr; // 特征探测点指针
        std::vector<SObject<T>*> m_members{};       // 本家族包含的全部 SObject 成员指针
    };
}