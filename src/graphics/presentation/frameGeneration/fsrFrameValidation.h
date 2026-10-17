#pragma once

#include "graphics/presentation/frameGeneration/temporalInputs.h"

namespace Libs::Graphics::FrameGeneration {

inline bool TemporalOutputAliasesInput(const TemporalInputs& frame) {
	return frame.output.image == frame.color.image || frame.output.image == frame.depth.image ||
	       frame.output.image == frame.motion.image;
}

// Return a diagnostic before enabling generation or recording SDK dispatches.
// The presentation compatibility path retains guest HUD/TAA; the offscreen
// probe requires explicitly HUD-free, native-size inputs instead.
inline const char* ValidateFsrPresentationFrame(const TemporalInputs& frame, uint32_t width,
                                                uint32_t height, bool upscale) {
	const auto guide = [&](const TemporalImage& image) {
		return image.image && image.width && image.height && image.width <= width &&
		       image.height <= height;
	};
	if (width == 0 || height == 0 || width > 16384 || height > 16384 || !guide(frame.depth) ||
	    !guide(frame.motion) || frame.depth.width != frame.motion.width ||
	    frame.depth.height != frame.motion.height || frame.depth.format != VK_FORMAT_R32_SFLOAT ||
	    frame.motion.format != VK_FORMAT_R16G16_SFLOAT)
		return "FSR temporal guides are missing or unsupported";
	if (!ValidTemporalCamera(frame, 100.0f)) return "FSR temporal camera or frame time is invalid";
	if (upscale) {
		if (!guide(frame.color) || frame.color.width != frame.depth.width ||
		    frame.color.height != frame.depth.height ||
		    (frame.color.format != VK_FORMAT_R8G8B8A8_UNORM &&
		     frame.color.format != VK_FORMAT_B8G8R8A8_UNORM &&
		     frame.color.format != VK_FORMAT_R8G8B8A8_SRGB &&
		     frame.color.format != VK_FORMAT_B8G8R8A8_SRGB))
			return "FSR upscaling color is missing or incompatible with the guides";
		if (!frame.output.image || frame.output.width != width || frame.output.height != height ||
		    frame.output.format != VK_FORMAT_R8G8B8A8_UNORM || TemporalOutputAliasesInput(frame))
			return "FSR upscaling output is missing, incompatible or aliases an input";
	}
	return nullptr;
}

inline const char* ValidateFsrProbeFrame(const TemporalInputs& frame, uint32_t width,
                                         uint32_t height, VkFormat format) {
	const auto image = [&](const TemporalImage& value) {
		return value.image && value.width == width && value.height == height &&
		       value.format != VK_FORMAT_UNDEFINED;
	};
	if (width == 0 || height == 0 || width > 16384 || height > 16384 ||
	    format != VK_FORMAT_R8G8B8A8_UNORM || !image(frame.color) || !image(frame.depth) ||
	    !image(frame.motion) || !image(frame.output) || frame.color.format != format ||
	    frame.output.format != format ||
	    (frame.depth.format != VK_FORMAT_R32_SFLOAT &&
	     frame.depth.format != VK_FORMAT_D32_SFLOAT) ||
	    (frame.motion.format != VK_FORMAT_R16G16_SFLOAT &&
	     frame.motion.format != VK_FORMAT_R32G32_SFLOAT) ||
	    TemporalOutputAliasesInput(frame))
		return "FSR3 probe requires separate native-size color, depth, motion and output images";
	if (!frame.hudless_scene || !ValidTemporalCamera(frame, 250.0f))
		return "FSR3 probe requires HUD-free scene color and valid temporal camera data";
	return nullptr;
}

} // namespace Libs::Graphics::FrameGeneration
