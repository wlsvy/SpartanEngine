/*
Copyright(c) 2016-2020 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

// Low frequency - Updates once per frame
cbuffer BufferFrame : register(b0)
{
    matrix g_view;
    matrix g_projection;
    matrix g_projectionOrtho;
    matrix g_viewProjection;
    matrix g_viewProjectionInv;
    matrix g_viewProjectionOrtho;
    matrix g_viewProjectionUnjittered;

    float g_delta_time;
    float g_time;
    float g_camera_near;
    float g_camera_far;
    
    float3 g_camera_position;
    float g_bloom_intensity;
    
    float g_sharpen_strength;   
    float3 g_camera_direction;
    
    float g_sharpen_clamp;
    float g_motionBlur_strength;
    float g_gamma;
    float g_toneMapping;
    
    float g_exposure;
    float g_directional_light_intensity;
    float g_ssr_enabled;
    float g_shadow_resolution;

    float2 g_taa_jitter_offset_previous;
    float2 g_taa_jitter_offset;
};

// Low frequency - Updates once per frame
static const int max_materials = 255;
cbuffer BufferMaterial : register(b1)
{
    float4 mat_clearcoat_clearcoatRough_aniso_anisoRot[max_materials];
    float4 mat_sheen_sheenTint_pad[max_materials];
}

// Medium frequency - Updates multiple times per frame
cbuffer BufferUber : register(b2)
{
    matrix g_transform;

    float4 g_color;
    
    float3 g_transform_axis;
    float g_blur_sigma;
    
    float2 g_blur_direction;
    float2 g_resolution;

    float4 g_mat_color;

    float2 g_mat_tiling;
    float2 g_mat_offset;

    float g_mat_roughness;
    float g_mat_metallic;
    float g_mat_normal;
    float g_mat_height;

    float g_mat_id;
    float3 g_padding;
};

// High frequency - Updates per object
cbuffer BufferObject : register(b3)
{
    matrix g_object_transform;
    matrix g_object_wvp_current;
    matrix g_object_wvp_previous;
};

// Updates as many times as there are lights
cbuffer LightBuffer : register(b4)
{
    matrix light_view_projection[6];
    float4 intensity_range_angle_bias;
    float3 color;
    float normal_bias;
    float4 position;
    float4 direction;
};
