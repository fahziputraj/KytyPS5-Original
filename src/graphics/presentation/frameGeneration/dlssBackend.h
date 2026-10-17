#pragma once

#include "graphics/presentation/frameGeneration/temporalInputs.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Libs::Graphics::FrameGeneration {

enum class DlssQuality { Quality, Balanced, Performance, UltraPerformance, Dlaa };

struct DlssOptions {
	uint32_t render_width = 0, render_height = 0;
	uint32_t output_width = 0, output_height = 0;
	DlssQuality quality = DlssQuality::Quality;
};

struct DlssApplication {
	// Supply the application's own GUID, never another game's NVIDIA app ID.
	std::string project_id, engine_version;
	std::filesystem::path data_directory, feature_directory;
};

struct DlssDevice {
	VkInstance instance = VK_NULL_HANDLE;
	VkPhysicalDevice physical = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;
	PFN_vkGetInstanceProcAddr get_instance_proc = nullptr;
	PFN_vkGetDeviceProcAddr get_device_proc = nullptr;
	uint32_t vendor_id = 0;
	std::vector<std::string> instance_extensions, device_extensions;
};

// Bindings describe the corresponding TemporalInputs image. The caller must
// create a matching 2D view and keep every image/view alive through execution.
struct DlssBinding {
	VkImageView view = VK_NULL_HANDLE;
	VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkImageUsageFlags usage = 0;
	VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
	VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
};

struct DlssFrame {
	TemporalInputs temporal;
	DlssBinding color, depth, motion, output;
};

const char* ValidateDlssApplication(const DlssApplication& application);
const char* ValidateDlssOptions(const DlssOptions& options);
const char* ValidateDlssFrame(const DlssFrame& frame, const DlssOptions& options);

// SDK boundary for the optional NGX implementation and CPU fault-injection
// tests. No vendor SDK types, DLL loading or Vulkan calls in the CPU backend.
class DlssSdk {
  public:
	virtual ~DlssSdk() = default;
	virtual bool Initialize(const DlssApplication&, const DlssDevice&, const DlssOptions&,
	                        std::string& error) = 0;
	virtual bool Create(VkCommandBuffer, const DlssOptions&, std::string& error) = 0;
	virtual bool Evaluate(VkCommandBuffer, const DlssFrame&, bool reset, std::string& error) = 0;
	virtual void Shutdown() = 0;
};

// Offscreen SR prototype: dedicated host command buffers, SDR RGBA8 color,
// forward-Z R32 depth, low-resolution unjittered RG16 motion and automatic
// exposure. HUD-free inputs are required. This does not replace a swapchain or
// advance guest flips, vblank or game time. Calls must be serialized by owner.
// The caller owns barriers/queue synchronization and discards a failed recording
// before falling back to its original image. No GPU work is submitted here.
// An SDK recording failure requires Initialize again: a discarded command may
// contain feature initialization, so its feature must never be reused blindly.
class DlssBackend final {
  public:
	~DlssBackend();
	DlssBackend() = default;
	DlssBackend(const DlssBackend&) = delete;
	DlssBackend& operator=(const DlssBackend&) = delete;
	bool Initialize(const DlssApplication& application, const DlssDevice& device,
	                const DlssOptions& options, std::unique_ptr<DlssSdk> sdk, std::string& error);
	bool Record(VkCommandBuffer command, const DlssFrame& frame, std::string& error);
	void ResetHistory();
	void Shutdown();

  private:
	std::unique_ptr<DlssSdk> m_sdk;
	DlssOptions m_options;
	TemporalHistory m_history;
	bool m_created = false;
	bool m_requires_reinitialize = false;
};

} // namespace Libs::Graphics::FrameGeneration
