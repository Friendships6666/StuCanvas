#pragma once

#include "appearance_types.hpp"

namespace StuCanvas
{
    struct AppearanceSimpleLine
    {
        AppearanceType type = AppearanceType::SimpleLine;
        float width = 3;
        float red = 1.0, green = 1.0, blue = 1.0, alpha = 1.0;
    };

    struct AppearanceSimplePoint
    {
        AppearanceType type = AppearanceType::SimplePoint;
        float radius = 3;
        float red = 1.0, green = 1.0, blue = 1.0, alpha = 1.0;
    };


    struct AppearanceOpenPBR_RAY
    {
        AppearanceType type = AppearanceType::OpenPBR_RayTracing;

        // --- A. Base Layer (基础层：金属或介质衬底) ---
        float base_weight = 1.0f;
        float base_color[ 3 ] = { 1.0f, 1.0f, 1.0f };
        float base_metalness = 0.0f;
        float base_diffuse_roughness = 0.0f;

        // --- B. Specular Lobe (高光物理反射层) ---
        float specular_weight = 1.0f;
        float specular_color[ 3 ] = { 1.0f, 1.0f, 1.0f };
        float specular_roughness = 0.3f;
        float specular_ior = 1.5f;
        float specular_anisotropy = 0.0f;

        // --- C. Subsurface Lobe (次表面散射层) ---
        float subsurface_weight = 0.0f;
        float subsurface_color[ 3 ] = { 1.0f, 1.0f, 1.0f };
        float subsurface_radius = 1.0f;
        float subsurface_radius_scale[ 3 ] = { 1.0f, 1.0f, 1.0f };

        // --- D. Transmission Lobe (物理透射层) ---
        float transmission_weight = 0.0f;
        float transmission_color[ 3 ] = { 1.0f, 1.0f, 1.0f };
        float transmission_depth = 1.0f;

        // --- E. Coat Layer (清漆覆膜层) ---
        float coat_weight = 0.0f;
        float coat_color[ 3 ] = { 1.0f, 1.0f, 1.0f };
        float coat_roughness = 0.1f;
        float coat_ior = 1.5f;

        // --- F. Sheen Lobe (茸毛层) ---
        float sheen_weight = 0.0f;
        float sheen_color[ 3 ] = { 1.0f, 1.0f, 1.0f };
        float sheen_roughness = 0.3f;

        // --- G. Emission Lobe (物理自发光层) ---
        float emission_weight = 0.0f;
        float emission_color[ 3 ] = { 0.0f, 0.0f, 0.0f };

        // --- H. Thin Film (薄膜干涉层) ---
        float thin_film_weight = 0.0f;
        float thin_film_thickness = 0.5f;

        // --- I. Geometry & Thin Walled (几何与薄壁设置) ---
        uint32_t geometry_thin_walled : 1 = 0;
    };

}   // namespace StuCanvas
