#pragma once

#include "graphics/presentation/frameGeneration/fsr3Backend.h"

struct ffxFunctions;

namespace Libs::Graphics::FrameGeneration {

using QueueSubmitCallback = VkResult (*)(uint32_t, const VkSubmitInfo*, VkFence);

// Uses AMD's replacement swapchain for generated-frame scheduling. Generated
// presents remain entirely host-side; callers complete one guest flip per real
// frame and retain the existing guest vblank/simulation clocks.
class FsrPresentation final {
public:
	FsrPresentation();
	~FsrPresentation();
	bool Initialize(VkDevice device, VkPhysicalDevice physical, PFN_vkGetDeviceProcAddr get_proc,
	                uint32_t width, uint32_t height, VkFormat format, bool upscale, bool generate,
	                std::string& error);
	// Initialize with an already loaded FidelityFX function table instead of the bundled
	// library, with the same validation. Tests use it with stub functions and fake handles.
	bool InitializeWithFunctions(const ffxFunctions& functions, VkDevice device,
	                             VkPhysicalDevice physical, PFN_vkGetDeviceProcAddr get_proc,
	                             uint32_t width, uint32_t height, VkFormat format, bool upscale,
	                             bool generate, std::string& error);
	bool CreateSwapchain(const VkSwapchainCreateInfoKHR& info, VkQueue game, VkQueue present,
	                     VkQueue acquire, VkQueue compute, uint32_t family,
	                     QueueSubmitCallback submit, VkSwapchainKHR& swapchain, std::string& error);
	bool OwnsSwapchain() const;
	VkResult GetImages(VkSwapchainKHR swapchain, uint32_t* count, VkImage* images);
	VkResult Acquire(VkSwapchainKHR swapchain, VkSemaphore semaphore, uint32_t* index);
	VkResult Present(VkQueue queue, const VkPresentInfoKHR& info);
	uint64_t PresentCount(VkSwapchainKHR swapchain) const;
	// Outputs are GENERAL before recording. Guides/color are COMPUTE_READ.
	// Returns true only when the optional upscaler wrote frame.output.
	bool Record(VkCommandBuffer command, const FsrFrame& frame, bool generate, std::string& error);
	void Suspend();
	void WaitForPresents();
	void Shutdown();

private:
	bool CreateContexts(const ffxFunctions& functions, VkDevice device, VkPhysicalDevice physical,
	                    PFN_vkGetDeviceProcAddr get_proc, uint32_t width, uint32_t height,
	                    VkFormat format, bool upscale, bool generate, std::string& error);

	struct Impl;
	std::unique_ptr<Impl> m;
};

} // namespace Libs::Graphics::FrameGeneration
