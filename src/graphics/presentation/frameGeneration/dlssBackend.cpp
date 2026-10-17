#include "graphics/presentation/frameGeneration/dlssBackend.h"

#include <array>

namespace Libs::Graphics::FrameGeneration {

const char* ValidateDlssApplication(const DlssApplication& application) {
	const auto& id = application.project_id;
	if (id.size() != 36)
		return "DLSS requires the application's own GUID project ID";
	for (size_t i = 0; i < id.size(); ++i) {
		const bool dash = i == 8 || i == 13 || i == 18 || i == 23;
		const bool hex = (id[i] >= '0' && id[i] <= '9') || (id[i] >= 'a' && id[i] <= 'f') ||
		                 (id[i] >= 'A' && id[i] <= 'F');
		if (dash ? id[i] != '-' : !hex)
			return "DLSS project ID is not GUID-shaped";
	}
	if (application.engine_version.empty() ||
	    application.engine_version.find('\0') != std::string::npos ||
	    !application.data_directory.is_absolute() || !application.feature_directory.is_absolute())
		return "DLSS requires an engine version and absolute data/feature directories";
	return nullptr;
}

const char* ValidateDlssOptions(const DlssOptions& options) {
	if (!options.render_width || !options.render_height || !options.output_width ||
	    !options.output_height || options.output_width > 16384 || options.output_height > 16384 ||
	    options.render_width > options.output_width ||
	    options.render_height > options.output_height)
		return "DLSS requires valid render dimensions no larger than its output";
	if (uint64_t(options.render_width) * options.output_height !=
	    uint64_t(options.render_height) * options.output_width)
		return "DLSS prototype requires matching render and output aspect ratios";
	switch (options.quality) {
	case DlssQuality::Quality:
	case DlssQuality::Balanced:
	case DlssQuality::Performance:
	case DlssQuality::UltraPerformance:
		break;
	case DlssQuality::Dlaa:
		if (options.render_width == options.output_width &&
		    options.render_height == options.output_height)
			return nullptr;
		return "DLAA requires matching render and output dimensions";
	default:
		return "Unknown DLSS quality selection";
	}
	return nullptr;
}

const char* ValidateDlssFrame(const DlssFrame& frame, const DlssOptions& options) {
	if (const auto* invalid = ValidateDlssOptions(options))
		return invalid;
	const auto& input = frame.temporal;
	const std::array<const TemporalImage*, 4> images{&input.color, &input.depth, &input.motion,
	                                                 &input.output};
	const std::array<const DlssBinding*, 4> bindings{&frame.color, &frame.depth, &frame.motion,
	                                                 &frame.output};
	const std::array<VkFormat, 4> formats{VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R32_SFLOAT,
	                                      VK_FORMAT_R16G16_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM};
	for (size_t i = 0; i < images.size(); ++i) {
		const auto& image = *images[i];
		const auto& binding = *bindings[i];
		const bool output = i == 3;
		if (!image.image || !binding.view || image.format != formats[i] ||
		    image.width != (output ? options.output_width : options.render_width) ||
		    image.height != (output ? options.output_height : options.render_height))
			return "DLSS image/view, format or dimensions do not match the configured SR inputs";
		const auto& range = binding.range;
		if (range.aspectMask != VK_IMAGE_ASPECT_COLOR_BIT || range.baseMipLevel != 0 ||
		    range.levelCount != 1 || range.baseArrayLayer != 0 || range.layerCount != 1 ||
		    binding.samples != VK_SAMPLE_COUNT_1_BIT)
			return "DLSS prototype requires single-sample, single-mip 2D color-aspect views";
		const auto usage = output ? VK_IMAGE_USAGE_STORAGE_BIT : VK_IMAGE_USAGE_SAMPLED_BIT;
		if (!(binding.usage & usage) ||
		    binding.layout !=
		        (output ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))
			return "DLSS resources must have sampled/storage usage and prepared read/general "
			       "layouts";
		for (size_t other = 0; other < i; ++other) {
			if (image.image == images[other]->image || binding.view == bindings[other]->view)
				return "DLSS resources must not alias each other";
		}
	}
	if (!input.hudless_scene || !ValidTemporalCamera(input, 100.0f))
		return "DLSS requires verified HUD-free color and valid temporal camera/frame metadata";
	return nullptr;
}

DlssBackend::~DlssBackend() { Shutdown(); }

bool DlssBackend::Initialize(const DlssApplication& application, const DlssDevice& device,
                             const DlssOptions& options, std::unique_ptr<DlssSdk> sdk,
                             std::string& error) {
	Shutdown();
	error.clear();
	if (const auto* invalid = ValidateDlssApplication(application)) {
		error = invalid;
		return false;
	}
	if (const auto* invalid = ValidateDlssOptions(options)) {
		error = invalid;
		return false;
	}
	if (device.vendor_id != 0x10de) {
		error = "DLSS requires a supported NVIDIA GPU";
		return false;
	}
	if (!device.instance || !device.physical || !device.device || !device.get_instance_proc ||
	    !device.get_device_proc || !sdk) {
		error = "DLSS Vulkan device, entry points or optional SDK implementation is unavailable";
		return false;
	}
	m_sdk = std::move(sdk);
	if (!m_sdk->Initialize(application, device, options, error)) {
		Shutdown();
		return false;
	}
	m_options = options;
	return true;
}

bool DlssBackend::Record(VkCommandBuffer command, const DlssFrame& frame, std::string& error) {
	error.clear();
	const char* invalid = m_requires_reinitialize
	                          ? "DLSS SDK recording failed; reinitialize before recording again"
	                      : !m_sdk || !command ? "DLSS context or command buffer is unavailable"
	                                           : ValidateDlssFrame(frame, m_options);
	if (!invalid && m_history.IsDuplicate(frame.temporal.id))
		invalid = "DLSS duplicate real frame rejected";
	if (invalid) {
		error = invalid;
		ResetHistory();
		return false;
	}
	if (!m_created) {
		if (!m_sdk->Create(command, m_options, error)) {
			m_requires_reinitialize = true;
			ResetHistory();
			return false;
		}
		m_created = true;
	}
	const auto& input = frame.temporal;
	const bool reset =
	    m_history.NeedsReset(input.id, input.depth.width, input.depth.height, input.reset, false);
	if (!m_sdk->Evaluate(command, frame, reset, error)) {
		m_requires_reinitialize = true;
		ResetHistory();
		return false;
	}
	m_history.Commit(input.id, input.depth.width, input.depth.height, false);
	return true;
}

void DlssBackend::ResetHistory() { m_history.Invalidate(); }

void DlssBackend::Shutdown() {
	if (m_sdk)
		m_sdk->Shutdown();
	m_sdk.reset();
	m_options = {};
	m_history = {};
	m_created = false;
	m_requires_reinitialize = false;
}

} // namespace Libs::Graphics::FrameGeneration
