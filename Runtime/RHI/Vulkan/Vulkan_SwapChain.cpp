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

//= IMPLEMENTATION ===============
#ifdef API_GRAPHICS_VULKAN
#include "../RHI_Implementation.h"
//================================

//= INCLUDES ========================
#include "../RHI_SwapChain.h"
#include "../RHI_Device.h"
#include "../RHI_CommandList.h"
#include "../RHI_Pipeline.h"
#include "../../Logging/Log.h"
#include "../../Math/MathHelper.h"
#include "../../Profiling/Profiler.h"
#include "../../Rendering/Renderer.h"
//===================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    namespace _Vulkan_SwapChain
    {
        inline bool create
        (
            RHI_Context* rhi_context,
            uint32_t* width,
            uint32_t* height,
            uint32_t buffer_count,
            RHI_Format format,
            uint32_t flags,
            void* window_handle,
            void*& surface_out,
            void*& swap_chain_view_out,
            array<void*, state_max_render_target_count>& resource_textures,
            array<void*, state_max_render_target_count>& resource_views,
            array<void*, state_max_render_target_count>& resource_views_acquiredSemaphore
        )
        {
            // Create surface
            VkSurfaceKHR surface = nullptr;
            {
                VkWin32SurfaceCreateInfoKHR create_info = {};
                create_info.sType                       = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
                create_info.hwnd                        = static_cast<HWND>(window_handle);
                create_info.hinstance                   = GetModuleHandle(nullptr);

                if (!vulkan_utility::error::check(vkCreateWin32SurfaceKHR(rhi_context->instance, &create_info, nullptr, &surface)))
                    return false;

                VkBool32 present_support = false;
                if (!vulkan_utility::error::check(vkGetPhysicalDeviceSurfaceSupportKHR(rhi_context->device_physical, rhi_context->queue_graphics_index, surface, &present_support)))
                    return false;

                if (!present_support)
                {
                    LOG_ERROR("The device does not support this kind of surface.");
                    return false;
                }
            }

            // Get surface capabilities
            VkSurfaceCapabilitiesKHR capabilities = vulkan_utility::surface::capabilities(surface);

            // Compute extent
            *width              = Math::Helper::Clamp(*width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
            *height             = Math::Helper::Clamp(*height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
            VkExtent2D extent   = { *width, *height };

            // Detect surface format and color space
            vulkan_utility::surface::detect_format_and_color_space(surface, &rhi_context->surface_format, &rhi_context->surface_color_space);

            // Swap chain
            VkSwapchainKHR swap_chain;
            {
                VkSwapchainCreateInfoKHR create_info    = {};
                create_info.sType                       = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
                create_info.surface                     = surface;
                create_info.minImageCount               = buffer_count;
                create_info.imageFormat                 = rhi_context->surface_format;
                create_info.imageColorSpace             = rhi_context->surface_color_space;
                create_info.imageExtent                 = extent;
                create_info.imageArrayLayers            = 1;
                create_info.imageUsage                  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

                uint32_t queueFamilyIndices[] = { rhi_context->queue_compute_index, rhi_context->queue_graphics_index };
                if (rhi_context->queue_compute_index != rhi_context->queue_graphics_index)
                {
                    create_info.imageSharingMode        = VK_SHARING_MODE_CONCURRENT;
                    create_info.queueFamilyIndexCount   = 2;
                    create_info.pQueueFamilyIndices     = queueFamilyIndices;
                }
                else
                {
                    create_info.imageSharingMode        = VK_SHARING_MODE_EXCLUSIVE;
                    create_info.queueFamilyIndexCount   = 0;
                    create_info.pQueueFamilyIndices     = nullptr;
                }

                create_info.preTransform    = capabilities.currentTransform;
                create_info.compositeAlpha  = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
                create_info.presentMode     = vulkan_utility::surface::set_present_mode(surface, flags);
                create_info.clipped         = VK_TRUE;
                create_info.oldSwapchain    = nullptr;

                if (!vulkan_utility::error::check(vkCreateSwapchainKHR(rhi_context->device, &create_info, nullptr, &swap_chain)))
                    return false;
            }

            // Images
            uint32_t image_count;
            vector<VkImage> swap_chain_images;
            {
                vkGetSwapchainImagesKHR(rhi_context->device, swap_chain, &image_count, nullptr);
                swap_chain_images.resize(image_count);
                vkGetSwapchainImagesKHR(rhi_context->device, swap_chain, &image_count, swap_chain_images.data());
            }

            // Image views
            {
                for (uint32_t i = 0; i < image_count; i++)
                {
                    resource_textures[i] = static_cast<void*>(swap_chain_images[i]);

                    // Name the image
                    vulkan_utility::debug::set_image_name(swap_chain_images[i], string(string("swapchain_image_") + to_string(0)).c_str());

                    if (!vulkan_utility::image::view::create(static_cast<void*>(swap_chain_images[i]), resource_views[i], VK_IMAGE_VIEW_TYPE_2D, rhi_context->surface_format, VK_IMAGE_ASPECT_COLOR_BIT))
                        return false;
                }
            }

            surface_out         = static_cast<void*>(surface);
            swap_chain_view_out = static_cast<void*>(swap_chain);

            for (uint32_t i = 0; i < buffer_count; i++)
            {
                vulkan_utility::semaphore::create(resource_views_acquiredSemaphore[i]);
            }

            return true;
        }

        inline void destroy(
            const RHI_Context* rhi_context,
            void*& surface,
            void*& swap_chain_view,
            array<void*, state_max_render_target_count>& image_views,
            array<void*, state_max_render_target_count>& semaphores_image_acquired
        )
        {
            // Semaphores
            for (auto& semaphore : semaphores_image_acquired)
            {
                vulkan_utility::semaphore::destroy(semaphore);
            }
            semaphores_image_acquired.fill(nullptr);

            // Image views
            vulkan_utility::image::view::destroy(image_views);

            // Swap chain view
            if (swap_chain_view)
            {
                vkDestroySwapchainKHR(rhi_context->device, static_cast<VkSwapchainKHR>(swap_chain_view), nullptr);
                swap_chain_view = nullptr;
            }

            // Surface
            if (surface)
            {
                vkDestroySurfaceKHR(rhi_context->instance, static_cast<VkSurfaceKHR>(surface), nullptr);
                surface = nullptr;
            }
        }
    }

	RHI_SwapChain::RHI_SwapChain(
		void* window_handle,
        const shared_ptr<RHI_Device>& rhi_device,
		const uint32_t width,
		const uint32_t height,
        const RHI_Format format		/*= Format_R8G8B8A8_UNORM */,
        const uint32_t buffer_count	/*= 1 */,
        const uint32_t flags		/*= Present_Immediate */
	)
	{
        // Validate device
        if (!rhi_device || !rhi_device->GetContextRhi()->device)
        {
            LOG_ERROR("Invalid device.");
            return;
        }

        // Validate resolution
        if (!rhi_device->ValidateResolution(width, height))
        {
            LOG_WARNING("%dx%d is an invalid resolution", width, height);
            return;
        }

        // Validate window handle
        const auto hwnd = static_cast<HWND>(window_handle);
        if (!hwnd || !IsWindow(hwnd))
        {
            LOG_ERROR_INVALID_PARAMETER();
            return;
        }

		// Copy parameters
		m_format		= format;
		m_rhi_device	= rhi_device.get();
		m_buffer_count	= buffer_count;
		m_width			= width;
		m_height		= height;
		m_window_handle	= window_handle;
        m_flags         = flags;

		m_initialized = _Vulkan_SwapChain::create
		(
            rhi_device->GetContextRhi(),
			&m_width,
			&m_height,
			m_buffer_count,
			m_format,
            m_flags,
			m_window_handle,
			m_surface,
			m_swap_chain_view,
            m_resource,
			m_resource_view,
			m_resource_view_acquiredSemaphore
		);

        // Create command pool
        vulkan_utility::command_pool::create(m_cmd_pool, RHI_Queue_Graphics);

        // Create command lists
        for (uint32_t i = 0; i < m_buffer_count; i++)
        {
            m_cmd_lists.emplace_back(make_shared<RHI_CommandList>(i, this, rhi_device->GetContext()));
        }
	}

	RHI_SwapChain::~RHI_SwapChain()
	{
		_Vulkan_SwapChain::destroy
		(
            m_rhi_device->GetContextRhi(),
			m_surface,
			m_swap_chain_view,
			m_resource_view,
			m_resource_view_acquiredSemaphore
		);

        // Clear command buffers
        m_cmd_lists.clear();

        // Command pool
        vulkan_utility::command_pool::destroy(m_cmd_pool);
	}

	bool RHI_SwapChain::Resize(const uint32_t width, const uint32_t height)
	{
        // Validate resolution
        m_present = m_rhi_device->ValidateResolution(width, height);
        if (!m_present)
        {
            // Return true as when minimizing, a resolution
            // of 0,0 can be passed in, and this is fine.
            return true;
        }

		// Only resize if needed
		if (m_width == width && m_height == height)
			return true;

		// Save new dimensions
		m_width		= width;
		m_height	= height;

		// Destroy previous swap chain
		_Vulkan_SwapChain::destroy
		(
            m_rhi_device->GetContextRhi(),
			m_surface,
			m_swap_chain_view,
			m_resource_view,
			m_resource_view_acquiredSemaphore
		);

		// Create the swap chain with the new dimensions
		m_initialized = _Vulkan_SwapChain::create
		(
            m_rhi_device->GetContextRhi(),
			&m_width,
			&m_height,
			m_buffer_count,
			m_format,
			m_flags,
			m_window_handle,
			m_surface,
			m_swap_chain_view,
            m_resource,
			m_resource_view,
			m_resource_view_acquiredSemaphore
		);

		return m_initialized;
	}

	bool RHI_SwapChain::AcquireNextImage()
	{
        if (!m_present)
            return true;

        // If we used all of our buffers, reset the command pool
        if (m_image_index + 1 > m_buffer_count)
        {
            VkCommandPool command_pool = static_cast<VkCommandPool>(m_cmd_pool);
            vulkan_utility::error::check(vkResetCommandPool(m_rhi_device->GetContextRhi()->device, command_pool, 0));
        }

		// Make index that always matches the m_image_index after vkAcquireNextImageKHR.
		// This is so getting semaphores and fences can be done by also simply using m_image_index.
		const uint32_t index = !m_image_acquired ? 0 : (m_image_index + 1) % m_buffer_count;
        
        // Acquire next image
        m_image_acquired = vulkan_utility::error::check(
            vkAcquireNextImageKHR(
                m_rhi_device->GetContextRhi()->device,
                static_cast<VkSwapchainKHR>(m_swap_chain_view),
                numeric_limits<uint64_t>::max(),
                static_cast<VkSemaphore>(m_resource_view_acquiredSemaphore[index]),
                nullptr,
                &m_image_index
            )
        );

        return m_image_acquired;
	}

	bool RHI_SwapChain::Present()
	{
        if (!m_present)
            return true;

        if (!m_image_acquired)
        {
            LOG_ERROR("Image has not been acquired");
            return false;
        }

        return m_rhi_device->Queue_Present(m_swap_chain_view, &m_image_index);
	}

    void RHI_SwapChain::SetLayout(RHI_Image_Layout layout, RHI_CommandList* command_list /*= nullptr*/)
    {
        if (m_layout == layout)
            return;

        if (command_list)
        {
            for (uint32_t i = 0; i < m_buffer_count; i++)
            {
                vulkan_utility::image::set_layout(command_list->GetResource_CommandBuffer(), m_resource[i], this, layout);
            }
        }

        m_layout = layout;
    }
}
#endif
