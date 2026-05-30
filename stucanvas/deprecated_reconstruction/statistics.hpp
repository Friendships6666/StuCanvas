/***************************************************************************
* Copyright (c) 2026 Tian Yuxuan (Friendships666)                          *
*                                                                          *
* Distributed under the terms of the MIT License.                          *
*                                                                          *
* The full license is in the file LICENSE, distributed with this software. *
***************************************************************************/

#pragma once

#include <algorithm>
#include <vector>
#include <cmath>
#include <limits>
#include <numeric>
#include <random>
#include <utility>
#include "../types/point.hpp"
#include "../types/deprecated_bounding_box.hpp"
namespace StuCanvas
{
    /**
     * @brief 计算2D点云中相邻点之间的平均距离 (顺序平均)
     * 注：对于无序点云，此值反映采样序列步长。
     */
    template <typename T>
    T CalculateAverageDistance(const std::vector<Point2D<T>>& points)
    {
        if (points.size() < 2) return 0;
        T total_dist = 0;
        for (size_t i = 0; i < points.size() - 1; ++i)
        {
            T dx = points[i + 1].x - points[i].x;
            T dy = points[i + 1].y - points[i].y;
            total_dist += std::sqrt(dx * dx + dy * dy);
        }
        return total_dist / static_cast<T>(points.size() - 1);
    }

    /**
     * @brief 计算2D点云的最小值和最大值点
     */
    template <typename T>
    std::pair<Point2D<T>, Point2D<T>> CalculateMinMax(const std::vector<Point2D<T>>& points)
    {
        Point2D<T> min_p{ std::numeric_limits<T>::max(), std::numeric_limits<T>::max() };
        Point2D<T> max_p{ std::numeric_limits<T>::lowest(), std::numeric_limits<T>::lowest() };

        for (const auto& p : points)
        {
            if (p.x < min_p.x) min_p.x = p.x;
            if (p.y < min_p.y) min_p.y = p.y;
            if (p.x > max_p.x) max_p.x = p.x;
            if (p.y > max_p.y) max_p.y = p.y;
        }
        return { min_p, max_p };
    }

    /**
     * @brief 计算2D点云重心
     */
    template <typename T>
    Point2D<T> CalculateCentroid(const std::vector<Point2D<T>>& points)
    {
        if (points.empty()) return { 0, 0 };
        T sum_x = 0, sum_y = 0;
        for (const auto& p : points)
        {
            sum_x += p.x;
            sum_y += p.y;
        }
        T count = static_cast<T>(points.size());
        return { sum_x / count, sum_y / count };
    }

    /**
     * @brief 计算2D总包围盒
     */
    template <typename T>
    BoundingBox2D<T> CalculateBoundingBox(const std::vector<Point2D<T>>& points)
    {
        auto [min_p, max_p] = CalculateMinMax(points);
        return { min_p, max_p };
    }

    // ==========================================
    // 3D 统计函数
    // ==========================================

    /**
     * @brief 计算3D点云中相邻点之间的平均距离
     */
    template <typename T>
    T CalculateAverageDistance(const std::vector<Point3D<T>>& points)
    {
        if (points.size() < 2) return 0;
        T total_dist = 0;
        for (size_t i = 0; i < points.size() - 1; ++i)
        {
            T dx = points[i + 1].x - points[i].x;
            T dy = points[i + 1].y - points[i].y;
            T dz = points[i + 1].z - points[i].z;
            total_dist += std::sqrt(dx * dx + dy * dy + dz * dz);
        }
        return total_dist / static_cast<T>(points.size() - 1);
    }

    /**
     * @brief 计算3D点云的最小值和最大值点
     */
    template <typename T>
    std::pair<Point3D<T>, Point3D<T>> CalculateMinMax(const std::vector<Point3D<T>>& points)
    {
        Point3D<T> min_p{ std::numeric_limits<T>::max(), std::numeric_limits<T>::max(), std::numeric_limits<T>::max() };
        Point3D<T> max_p{ std::numeric_limits<T>::lowest(), std::numeric_limits<T>::lowest(), std::numeric_limits<T>::lowest() };

        for (const auto& p : points)
        {
            if (p.x < min_p.x) min_p.x = p.x;
            if (p.y < min_p.y) min_p.y = p.y;
            if (p.z < min_p.z) min_p.z = p.z;
            if (p.x > max_p.x) max_p.x = p.x;
            if (p.y > max_p.y) max_p.y = p.y;
            if (p.z > max_p.z) max_p.z = p.z;
        }
        return { min_p, max_p };
    }

    /**
     * @brief 计算3D点云重心
     */
    template <typename T>
    Point3D<T> CalculateCentroid(const std::vector<Point3D<T>>& points)
    {
        if (points.empty()) return { 0, 0, 0 };
        T sum_x = 0, sum_y = 0, sum_z = 0;
        for (const auto& p : points)
        {
            sum_x += p.x;
            sum_y += p.y;
            sum_z += p.z;
        }
        T count = static_cast<T>(points.size());
        return { sum_x / count, sum_y / count, sum_z / count };
    }

    /**
     * @brief 计算3D总包围盒
     */
    template <typename T>
    BoundingBox3D<T> CalculateBoundingBox(const std::vector<Point3D<T>>& points)
    {
        auto [min_p, max_p] = CalculateMinMax(points);
        return { min_p, max_p };
    }



    /**
     * @brief 2D 重载: 通过多次随机打乱计算平均距离，用于评估全局点云密度
     * @param points 点云副本 (内部打乱)
     * @param repetitions 重复打乱并计算的次数
     */
    template <typename T>
    T CalculateAverageDistance(std::vector<Point2D<T>> points, size_t repetitions)
    {
        if (points.size() < 2 || repetitions == 0) return 0;

        std::random_device rd;
        std::mt19937 g(rd());
        T total_avg = 0;

        for (size_t i = 0; i < repetitions; ++i)
        {
            std::shuffle(points.begin(), points.end(), g);
            // 调用之前定义的基础引用版本
            total_avg += CalculateAverageDistance(static_cast<const std::vector<Point2D<T>>&>(points));
        }

        return total_avg / static_cast<T>(repetitions);
    }

    /**
     * @brief 3D 重载: 通过多次随机打乱计算平均距离，用于评估全局点云密度
     * @param points 点云副本 (内部打乱)
     * @param repetitions 重复打乱并计算的次数
     */
    template <typename T>
    T CalculateAverageDistance(std::vector<Point3D<T>> points, size_t repetitions)
    {
        if (points.size() < 2 || repetitions == 0) return 0;

        std::random_device rd;
        std::mt19937 g(rd());
        T total_avg = 0;

        for (size_t i = 0; i < repetitions; ++i)
        {
            std::shuffle(points.begin(), points.end(), g);
            // 调用之前定义的基础引用版本
            total_avg += CalculateAverageDistance(static_cast<const std::vector<Point3D<T>>&>(points));
        }

        return total_avg / static_cast<T>(repetitions);
    }


    template <typename T>
    Point2D<T> CalculateVariance(const std::vector<Point2D<T>>& points)
    {
        if (points.empty()) return { 0, 0 };

        Point2D<T> mean = CalculateCentroid(points);
        T sum_sq_x = 0;
        T sum_sq_y = 0;

        for (const auto& p : points)
        {
            T dx = p.x - mean.x;
            T dy = p.y - mean.y;
            sum_sq_x += dx * dx;
            sum_sq_y += dy * dy;
        }

        T n = static_cast<T>(points.size());
        return { sum_sq_x / n, sum_sq_y / n };
    }

    /**
     * @brief 3D: 计算点云各轴向的分量方差
     * @return Point3D 包含 (x轴方差, y轴方差, z轴方差)
     */
    template <typename T>
    Point3D<T> CalculateVariance(const std::vector<Point3D<T>>& points)
    {
        if (points.empty()) return { 0, 0, 0 };

        Point3D<T> mean = CalculateCentroid(points);
        T sum_sq_x = 0;
        T sum_sq_y = 0;
        T sum_sq_z = 0;

        for (const auto& p : points)
        {
            T dx = p.x - mean.x;
            T dy = p.y - mean.y;
            T dz = p.z - mean.z;
            sum_sq_x += dx * dx;
            sum_sq_y += dy * dy;
            sum_sq_z += dz * dz;
        }

        T n = static_cast<T>(points.size());
        return { sum_sq_x / n, sum_sq_y / n, sum_sq_z / n };
    }
}