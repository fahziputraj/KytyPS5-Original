#pragma once

#include <cstdint>
#include <string>

namespace Libs::Graphics::FrameGeneration {

// FSR presentation outcomes for one reporting window, reset when the swapchain is recreated.
// Every count except `sdk_presents` is a host-side decision or a recording into a command buffer
// that is then submitted; none proves GPU execution or what reached the screen. `sdk_presents`
// is the replacement swapchain's own present count, so presents beyond the application's are
// generated frames. Never read these as guest-frame rate or game pace.
struct TemporalCounters {
	uint64_t presents            = 0; // application presents (new guest frames and redraws)
	uint64_t sdk_presents        = 0; // replacement-swapchain presents; 0 without it
	uint64_t new_frames          = 0; // new guest frames with valid guides, sent to the SDK
	uint64_t missing_guides      = 0; // no valid guides: original image
	uint64_t redraws             = 0; // same guest frame again (pause/overlay): no SDK work
	uint64_t timing_rejected     = 0; // long frame gap or oversized guides: original image
	uint64_t record_failed       = 0; // validation or SDK failure: suspended, original image
	uint64_t upscaled            = 0; // SR output recorded and presented instead of the original
	uint64_t generation_prepared = 0; // FG prepare recorded for the replacement swapchain

	void Reset() { *this = {}; }

	[[nodiscard]] std::string Format(bool replacement_swapchain) const {
		auto text =
		    "FSR window: " + std::to_string(presents) + " presents (" + std::to_string(new_frames) +
		    " new guest frames, " + std::to_string(redraws) + " redraws, " +
		    std::to_string(missing_guides) + " without guides, " + std::to_string(timing_rejected) +
		    " timing-rejected, " + std::to_string(record_failed) + " failed); recorded SR " +
		    std::to_string(upscaled) + ", FG prepare " + std::to_string(generation_prepared);
		if (!replacement_swapchain) return text + "; native swapchain, no generated presents";
		text += "; SDK swapchain presented " + std::to_string(sdk_presents);
		if (sdk_presents >= presents)
			return text + " (about " + std::to_string(sdk_presents - presents) + " generated)";
		return text + " (fewer than the application presents: none generated)";
	}
};

} // namespace Libs::Graphics::FrameGeneration
