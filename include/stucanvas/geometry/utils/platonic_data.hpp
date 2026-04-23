#pragma once
#include <vector>
#include <cmath>
namespace StuCanvas::StaticData
    {
        template <typename T>
        struct PlatonicRaw
        {
            std::vector<std::vector<T>> vertices;
            std::vector<std::vector<size_t>> faces;
        };

        template <typename T>
        struct PlatonicLibrary
        {
            static inline const T phi = (static_cast<T>(1) + std::sqrt(static_cast<T>(5))) / static_cast<T>(2);
            static inline const T inv_phi = static_cast<T>(1) / phi;


            static PlatonicRaw<T> Get(int type)
            {
                switch (type)
                {
                case 4: return GetTetrahedron();
                case 6: return GetHexahedron();
                case 8: return GetOctahedron();
                case 12: return GetDodecahedron();
                case 20: return GetIcosahedron();
                default: return {};
                }
            }

        private:
            static PlatonicRaw<T> GetTetrahedron()
            {
                return {
                    {{1, 1, 1}, {1, -1, -1}, {-1, 1, -1}, {-1, -1, 1}},
                    {{0, 1, 2}, {0, 1, 3}, {0, 2, 3}, {1, 3, 2}}
                };
            }


            static PlatonicRaw<T> GetHexahedron() {
                return {
            {
                {-1,-1,-1}, {1,-1,-1}, {1,1,-1}, {-1,1,-1}, // 底面 0,1,2,3
                {-1,-1, 1}, {1,-1, 1}, {1,1, 1}, {-1,1, 1}  // 顶面 4,5,6,7
            },
            {
                {0, 3, 2, 1}, // 底面 (右手定则朝下)
                {4, 5, 6, 7}, // 顶面 (右手定则朝上)
                {0, 1, 5, 4}, // 前面
                {1, 2, 6, 5}, // 右面
                {2, 3, 7, 6}, // 后面
                {3, 0, 4, 7}  // 左面
            }
                };
            }


            static PlatonicRaw<T> GetOctahedron()
            {
                return {
                    {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}},
                    {{4, 0, 2}, {4, 2, 1}, {4, 1, 3}, {4, 3, 0}, {5, 0, 3}, {5, 3, 1}, {5, 1, 2}, {5, 2, 0}}
                };
            }


            static PlatonicRaw<T> GetDodecahedron()
            {
                std::vector<std::vector<T>> v = {
                    {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1}, {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1},

                    {0, -inv_phi, -phi}, {0, inv_phi, -phi}, {0, -inv_phi, phi}, {0, inv_phi, phi},
                    {-inv_phi, -phi, 0}, {inv_phi, -phi, 0}, {-inv_phi, phi, 0}, {inv_phi, phi, 0},
                    {-phi, 0, -inv_phi}, {phi, 0, -inv_phi}, {-phi, 0, inv_phi}, {phi, 0, inv_phi}
                };
                return {
                    v, {
                        {0, 16, 18, 4, 12}, {1, 13, 5, 19, 17}, {2, 15, 6, 19, 17}, {3, 14, 7, 18, 16},
                        {0, 8, 9, 3, 16}, {1, 8, 9, 2, 17}, {4, 10, 11, 7, 18}, {5, 10, 11, 6, 19},
                        {0, 12, 13, 1, 8}, {3, 14, 15, 2, 9}, {4, 12, 13, 5, 10}, {7, 14, 15, 6, 11}
                    }
                };
            }


            static PlatonicRaw<T> GetIcosahedron() {
                const T phi = (1.0 + std::sqrt(5.0)) / 2.0;
                std::vector<std::vector<T>> v = {
                    {-1,  phi,  0}, { 1,  phi,  0}, {-1, -phi,  0}, { 1, -phi,  0}, // 0,1,2,3
                    { 0, -1,  phi}, { 0,  1,  phi}, { 0, -1, -phi}, { 0,  1, -phi}, // 4,5,6,7
                    { phi,  0, -1}, { phi,  0,  1}, {-phi,  0, -1}, {-phi,  0,  1}  // 8,9,10,11
                };

                // 20个三角形面（严格逆时针绕行，法线朝外）
                return { v, {
            {0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11},
            {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
            {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9},
            {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}
                }};
            }
        };
    }
