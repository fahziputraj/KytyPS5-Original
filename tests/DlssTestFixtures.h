#pragma once

#include "graphics/presentation/frameGeneration/dlssBackend.h"

namespace DlssTests {
using namespace Libs::Graphics::FrameGeneration;

template <class T> T Handle(uintptr_t value) { return reinterpret_cast<T>(value); }
inline VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL NoInstanceProc(VkInstance, const char*) {
	return nullptr;
}
inline VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL NoDeviceProc(VkDevice, const char*) {
	return nullptr;
}

inline DlssApplication Application() {
	// Test fixture only. No NGX application identity or SDK runtime is used.
	return {"86294c1e-06d4-45d1-91c1-40ae92ea4091", "test-1", "C:/dlss-test-data",
	        "C:/dlss-test-features"};
}
inline DlssOptions Options() { return {1280, 720, 1920, 1080, DlssQuality::Quality}; }
inline DlssDevice Device() {
	return {Handle<VkInstance>(1),
	        Handle<VkPhysicalDevice>(2),
	        Handle<VkDevice>(3),
	        NoInstanceProc,
	        NoDeviceProc,
	        0x10de,
	        {},
	        {}};
}
inline DlssFrame Frame() {
	DlssFrame frame;
	auto& input = frame.temporal;
	input.color = {Handle<VkImage>(11), VK_FORMAT_R8G8B8A8_UNORM, 1280, 720};
	input.depth = {Handle<VkImage>(12), VK_FORMAT_R32_SFLOAT, 1280, 720};
	input.motion = {Handle<VkImage>(13), VK_FORMAT_R16G16_SFLOAT, 1280, 720};
	input.output = {Handle<VkImage>(14), VK_FORMAT_R8G8B8A8_UNORM, 1920, 1080};
	input.id = 7;
	input.milliseconds = 1000.0f / 60.0f;
	input.near_plane = 0.1f;
	input.far_plane = 1000;
	input.vertical_fov = input.view_space_to_meters = 1.0f;
	input.camera_right = {1, 0, 0};
	input.camera_up = {0, 1, 0};
	input.camera_forward = {0, 0, -1};
	input.jitter_x = 0.25f;
	input.jitter_y = -0.125f;
	input.motion_scale_x = 1280;
	input.motion_scale_y = -720;
	input.hudless_scene = true;
	frame.color = {Handle<VkImageView>(21), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	               VK_IMAGE_USAGE_SAMPLED_BIT};
	frame.depth = {Handle<VkImageView>(22), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	               VK_IMAGE_USAGE_SAMPLED_BIT};
	frame.motion = {Handle<VkImageView>(23), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	                VK_IMAGE_USAGE_SAMPLED_BIT};
	frame.output = {Handle<VkImageView>(24), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_USAGE_STORAGE_BIT};
	return frame;
}
} // namespace DlssTests
