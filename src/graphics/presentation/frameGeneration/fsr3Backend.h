#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vulkan/vulkan.h>

namespace Libs::Graphics::FrameGeneration {

struct FsrImage {
	VkImage  image  = VK_NULL_HANDLE;
	VkFormat format = VK_FORMAT_UNDEFINED;
	uint32_t width = 0, height = 0;
};

struct FsrFrame {
	FsrImage             color, depth, motion, output;
	uint64_t             id           = 0;
	float                milliseconds = 0.0f;
	float                near_plane = 0.0f, far_plane = 0.0f, vertical_fov = 0.0f;
	float                jitter_x = 0.0f, jitter_y = 0.0f;
	float                motion_scale_x = 1.0f, motion_scale_y = 1.0f;
	float                view_space_to_meters = 0.0f;
	std::array<float, 3> camera_position {}, camera_right {}, camera_up {}, camera_forward {};
	bool                 hudless_scene = false;
	bool                 reset         = false;
};

// Prototype backend for a future verified guest-input provider. It records into
// a dedicated host command buffer, never the guest's renderer recording. Inputs
// must be shader-read-only and output GENERAL, with valid GPU lifetimes through
// execution. This first prototype supports native-size, SDR, finite forward-Z
// guides. The caller owns synchronization, frame pacing and UI composition.
// No guest flip, vblank or simulation clock is advanced by generated frames.
class Fsr3Backend final {
public:
	Fsr3Backend();
	~Fsr3Backend();
	Fsr3Backend(const Fsr3Backend&)            = delete;
	Fsr3Backend& operator=(const Fsr3Backend&) = delete;

	bool Initialize(const std::filesystem::path& library, VkDevice device,
	                VkPhysicalDevice physical, PFN_vkGetDeviceProcAddr get_proc, uint32_t width,
	                uint32_t height, VkFormat format, std::string& error);
	bool Record(VkCommandBuffer command, const FsrFrame& frame, std::string& error);
	void ResetHistory();
	void Shutdown();

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
};

} // namespace Libs::Graphics::FrameGeneration
