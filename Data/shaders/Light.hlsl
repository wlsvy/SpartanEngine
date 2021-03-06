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

//= INCLUDES =====================      
#include "BRDF.hlsl"              
#include "ShadowMapping.hlsl"
#include "VolumetricLighting.hlsl"
//================================

struct PixelOutputType
{
    float3 diffuse      : SV_Target0;
    float3 specular     : SV_Target1;
    float3 volumetric   : SV_Target2;
};

PixelOutputType mainPS(Pixel_PosUv input)
{
    PixelOutputType light_out;
    light_out.diffuse       = 0.0f;
    light_out.specular      = 0.0f;
    light_out.volumetric    = 0.0f;

    // Sample normal
    float4 normal_sample = tex_normal.Sample(sampler_point_clamp, input.uv);

    // Fill surface struct
    Surface surface;
    surface.uv                      = input.uv;
    surface.depth                   = tex_depth.Sample(sampler_point_clamp, surface.uv).r;
    surface.position                = get_position(surface.depth, surface.uv);
    surface.normal                  = normal_decode(normal_sample.xyz);
    surface.camera_to_pixel         = normalize(surface.position - g_camera_position.xyz);
    surface.camera_to_pixel_length  = length(surface.position - g_camera_position.xyz);

    // Create material
    Material material;
    {
        float4 sample_albedo    = tex_albedo.Sample(sampler_point_clamp, input.uv);
        float4 sample_material  = tex_material.Sample(sampler_point_clamp, input.uv);
        int mat_id              = round(sample_material.a * 255);

        material.albedo                 = sample_albedo.rgb;
        material.roughness              = sample_material.r;
        material.metallic               = sample_material.g;
        material.emissive               = sample_material.b;
        material.clearcoat              = mat_clearcoat_clearcoatRough_aniso_anisoRot[mat_id].x;
        material.clearcoat_roughness    = mat_clearcoat_clearcoatRough_aniso_anisoRot[mat_id].y;
        material.anisotropic            = mat_clearcoat_clearcoatRough_aniso_anisoRot[mat_id].z;
        material.anisotropic_rotation   = mat_clearcoat_clearcoatRough_aniso_anisoRot[mat_id].w;
        material.sheen                  = mat_sheen_sheenTint_pad[mat_id].x;
        material.sheen_tint             = mat_sheen_sheenTint_pad[mat_id].y;
        material.occlusion              = min(normal_sample.a, tex_ssao.Sample(sampler_point_clamp, input.uv).r); // min(occlusion, ssao)
        material.F0                     = lerp(0.04f, material.albedo, material.metallic);
        material.is_transparent         = sample_albedo.a != 1.0f;
        material.is_sky                 = mat_id == 0;
    }

    // Fill light struct
    Light light;
    light.color             = color.xyz;
    light.position          = position.xyz;
    light.intensity         = intensity_range_angle_bias.x;
    light.range             = intensity_range_angle_bias.y;
    light.angle             = intensity_range_angle_bias.z;
    light.bias              = intensity_range_angle_bias.w;
    light.normal_bias       = normal_bias;
    light.distance_to_pixel = length(surface.position - light.position);
    #if DIRECTIONAL
    light.array_size    = 4;
    light.direction     = direction.xyz; 
    light.attenuation   = 1.0f;
    #elif POINT
    light.array_size    = 1;
    light.direction     = normalize(surface.position - light.position);
    light.attenuation   = saturate(1.0f - (light.distance_to_pixel / light.range)); light.attenuation *= light.attenuation;    
    #elif SPOT
    light.array_size    = 1;
    light.direction     = normalize(surface.position - light.position);
    float cutoffAngle   = 1.0f - light.angle;
    float theta         = dot(direction.xyz, light.direction);
    float epsilon       = cutoffAngle - cutoffAngle * 0.9f;
    light.attenuation   = saturate((theta - cutoffAngle) / epsilon); // attenuate when approaching the outer cone
    light.attenuation   *= saturate(1.0f - light.distance_to_pixel / light.range); light.attenuation *= light.attenuation;
    #endif
    light.intensity     *= light.attenuation;
    
    // Shadow 
    {
        float4 shadow = 1.0f;
        
        // Shadow mapping
        #if SHADOWS
        {
            shadow = Shadow_Map(surface, light, material.is_transparent);

            // Volumetric lighting (requires shadow maps)
            #if VOLUMETRIC
            {
                light_out.volumetric.rgb = VolumetricLighting(surface, light);
            }
            #endif
        }
        #endif
        
        // Screen space shadows
        #if SHADOWS_SCREEN_SPACE
        {
            shadow.a = min(shadow.a, ScreenSpaceShadows(surface, light));
        }
        #endif
    
        // Occlusion from texture and ssao
        shadow.a = min(shadow.a, material.occlusion);
        
        // Modulate light intensity and color
        light.intensity *= shadow.a;
        light.color     *= shadow.rgb;
    }

    // Reflectance equation
    [branch]
    if (light.intensity > 0.0f && !material.is_sky)
    {
        // Compute some vectors and dot products
        float3 l        = -light.direction;
        float3 v        = -surface.camera_to_pixel;
        float3 h        = normalize(v + l);
        float l_dot_h   = saturate(dot(l, h));
        float v_dot_h   = saturate(dot(v, h));
        float n_dot_v   = saturate(dot(surface.normal, v));
        float n_dot_l   = saturate(dot(surface.normal, l));
        float n_dot_h   = saturate(dot(surface.normal, h));

        // Specular
        float3 specular         = 0.0f;
        float3 specular_fresnel = 0.0f;
        if (material.anisotropic == 0.0f)
        {
            specular = BRDF_Specular_Isotropic(material, n_dot_v, n_dot_l, n_dot_h, v_dot_h, specular_fresnel);
        }
        else
        {
            specular = BRDF_Specular_Anisotropic(material, surface, v, l, h, n_dot_v, n_dot_l, n_dot_h, l_dot_h, specular_fresnel);
        }
        float3 specular_energy_cons = energy_conservation(specular_fresnel, material.metallic);

        // Specular clearcoat
        float3 specular_clearcoat               = 0.0f;
        float3 specular_clearcoat_fresnel       = 0.0f;
        float3 specular_clearcoat_energy_cons   = 1.0f;
        if (material.clearcoat != 0.0f)
        {
            specular_clearcoat              = BRDF_Specular_Clearcoat(material, n_dot_h, v_dot_h, specular_clearcoat_fresnel);
            specular_clearcoat_energy_cons  = energy_conservation(specular_clearcoat_fresnel);
        }

        // Sheen
        float3 specular_sheen               = 0.0f;
        float3 specular_sheen_fresnel       = 0.0f;
        float3 specular_sheen_energy_cons   = 1.0f;
        if (material.sheen != 0.0f)
        {
            specular_sheen              = BRDF_Specular_Sheen(material, n_dot_v, n_dot_l, n_dot_h, specular_sheen_fresnel);
            specular_sheen_energy_cons  = energy_conservation(specular_sheen_fresnel);
        }
        
        // Diffuse
        float3 diffuse = BRDF_Diffuse(material, n_dot_v, n_dot_l, v_dot_h);

        // Conserve energy
        diffuse *= specular_energy_cons * specular_clearcoat_energy_cons * specular_sheen_energy_cons;

        // SSR
        float3 light_reflection = 0.0f;
        #if SCREEN_SPACE_REFLECTIONS
        float2 sample_ssr = tex_ssr.Sample(sampler_point_clamp, input.uv).xy;
        [branch]
        if (sample_ssr.x * sample_ssr.y != 0.0f)
        {
            // saturate as reflections will accumulate int tex_frame overtime, causing more light to go out that it comes in.
            float3 ssr          = saturate(tex_frame.Sample(sampler_bilinear_clamp, sample_ssr.xy).rgb);
            light_reflection    = ssr * specular_fresnel;
            light_reflection    += ssr * specular_clearcoat_fresnel;
            light_reflection    *= 1.0f - material.roughness; // fade with roughness as we don't have blurry screen space reflections yet
        }
        #endif

        // Radiance
        float3 radiance = light.color * light.intensity * n_dot_l;
        
        light_out.diffuse.rgb   = saturate_16(diffuse * radiance);
        light_out.specular.rgb  = saturate_16((specular + specular_clearcoat + specular_sheen) * radiance + light_reflection);
    }

    return light_out;
}
