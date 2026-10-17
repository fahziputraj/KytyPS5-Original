#pragma once

#include "graphics/presentation/frameGeneration/temporalInputs.h"

#include <filesystem>
#include <memory>
#include <string>

namespace Libs::Graphics::FrameGeneration {

using FsrImage = TemporalImage;
using FsrFrame = TemporalInputs;

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
