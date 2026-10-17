#include "graphics/presentation/frameGeneration/fsrFrameValidation.h"
#include "graphics/presentation/frameGeneration/temporalCounters.h"

#include <cstdio>
#include <limits>

using namespace Libs::Graphics::FrameGeneration;

namespace {
TemporalInputs ValidFrame() {
	// Descriptor-only tests: fake handles are never submitted to a Vulkan driver.
	TemporalInputs frame;
	frame.color  = {reinterpret_cast<VkImage>(uintptr_t(1)), VK_FORMAT_R8G8B8A8_UNORM, 1920, 1080};
	frame.depth  = {reinterpret_cast<VkImage>(uintptr_t(2)), VK_FORMAT_R32_SFLOAT, 1920, 1080};
	frame.motion = {reinterpret_cast<VkImage>(uintptr_t(3)), VK_FORMAT_R16G16_SFLOAT, 1920, 1080};
	frame.output = {reinterpret_cast<VkImage>(uintptr_t(4)), VK_FORMAT_R8G8B8A8_UNORM, 1920, 1080};
	frame.milliseconds         = 1000.0f / 60;
	frame.near_plane           = 0.1f;
	frame.far_plane            = 1500;
	frame.vertical_fov         = 1.0f;
	frame.view_space_to_meters = 1;
	frame.camera_right         = {1, 0, 0};
	frame.camera_up            = {0, 1, 0};
	frame.camera_forward       = {0, 0, -1};
	frame.hudless_scene        = true;
	return frame;
}
} // namespace

int main() {
	int        failures = 0, checks = 0;
	const auto check = [&](bool ok, const char* name) {
		++checks;
		if (!ok) {
			std::fprintf(stderr, "FAIL: %s\n", name);
			++failures;
		}
	};
	const auto presentation = [](const TemporalInputs& frame, bool upscale = true) {
		return ValidateFsrPresentationFrame(frame, 1920, 1080, upscale) == nullptr;
	};
	const auto probe = [](const TemporalInputs& frame) {
		return ValidateFsrProbeFrame(frame, 1920, 1080, VK_FORMAT_R8G8B8A8_UNORM) == nullptr;
	};
	const auto valid = ValidFrame();
	check(presentation(valid) && probe(valid), "valid native-size frame accepted by both paths");
	check(!presentation({}) && !probe({}), "absent game guides rejected");
	auto scaled        = valid;
	scaled.color.width = scaled.depth.width = scaled.motion.width = 1280;
	scaled.color.height = scaled.depth.height = scaled.motion.height = 720;
	check(presentation(scaled), "matching low-resolution guides and full-resolution output");
	check(!probe(scaled), "native-size probe does not silently accept upscaling inputs");
	auto retained_hud          = valid;
	retained_hud.hudless_scene = false;
	check(presentation(retained_hud) && !probe(retained_hud),
	      "HUD-retaining compatibility path remains distinct from HUD-free probe");
	auto generation_only  = valid;
	generation_only.color = generation_only.output = {};
	check(presentation(generation_only, false), "FG-only swapchain path needs no SR output");
	check(!presentation(generation_only), "requested SR cannot silently skip absent output");
	for (const auto image: {&TemporalInputs::color, &TemporalInputs::depth, &TemporalInputs::motion,
	                        &TemporalInputs::output}) {
		auto broken           = valid;
		(broken.*image).image = VK_NULL_HANDLE;
		check(!presentation(broken) && !probe(broken), "missing resource rejected");
		broken                = valid;
		(broken.*image).width = 0;
		check(!presentation(broken) && !probe(broken), "empty resource rejected");
		broken                 = valid;
		(broken.*image).height = 2048;
		check(!presentation(broken) && !probe(broken), "mismatched or oversized resource rejected");
		broken                 = valid;
		(broken.*image).format = VK_FORMAT_UNDEFINED;
		check(!presentation(broken) && !probe(broken), "unknown resource format rejected");
	}
	for (const auto image:
	     {&TemporalInputs::color, &TemporalInputs::depth, &TemporalInputs::motion}) {
		auto alias         = valid;
		alias.output.image = (alias.*image).image;
		check(!presentation(alias) && !probe(alias), "output must not overwrite an input");
	}
	for (const auto format:
	     {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB}) {
		auto color         = valid;
		color.color.format = format;
		check(presentation(color), "supported SDR presentation color layouts");
		check(!probe(color), "probe keeps its stricter RGBA8 contract");
	}
	auto wider_probe          = valid;
	wider_probe.depth.format  = VK_FORMAT_D32_SFLOAT;
	wider_probe.motion.format = VK_FORMAT_R32G32_SFLOAT;
	check(probe(wider_probe) && !presentation(wider_probe),
	      "probe-only guide formats stay restricted");
	for (const float invalid:
	     {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
	      -std::numeric_limits<float>::infinity()}) {
		for (const auto field:
		     {&TemporalInputs::milliseconds, &TemporalInputs::near_plane,
		      &TemporalInputs::far_plane, &TemporalInputs::vertical_fov, &TemporalInputs::jitter_x,
		      &TemporalInputs::jitter_y, &TemporalInputs::motion_scale_x,
		      &TemporalInputs::motion_scale_y, &TemporalInputs::view_space_to_meters}) {
			auto broken   = valid;
			broken.*field = invalid;
			check(!presentation(broken) && !probe(broken), "non-finite temporal metadata rejected");
		}
		for (const auto field: {&TemporalInputs::camera_position, &TemporalInputs::camera_right,
		                        &TemporalInputs::camera_up, &TemporalInputs::camera_forward}) {
			for (int axis = 0; axis < 3; ++axis) {
				auto broken           = valid;
				(broken.*field)[axis] = invalid;
				check(!presentation(broken) && !probe(broken), "non-finite camera basis rejected");
			}
		}
	}
	for (const auto field:
	     {&TemporalInputs::milliseconds, &TemporalInputs::near_plane, &TemporalInputs::vertical_fov,
	      &TemporalInputs::motion_scale_x, &TemporalInputs::motion_scale_y,
	      &TemporalInputs::view_space_to_meters}) {
		auto broken   = valid;
		broken.*field = 0;
		check(!presentation(broken) && !probe(broken), "zero time, scale or projection rejected");
	}
	auto broken_camera      = valid;
	broken_camera.far_plane = broken_camera.near_plane;
	check(!presentation(broken_camera) && !probe(broken_camera),
	      "degenerate clipping range rejected");
	broken_camera              = valid;
	broken_camera.vertical_fov = 3.14159f;
	check(!presentation(broken_camera) && !probe(broken_camera), "invalid FOV rejected");
	broken_camera              = valid;
	broken_camera.camera_right = {2, 0, 0};
	check(!presentation(broken_camera) && !probe(broken_camera), "non-unit camera basis rejected");
	broken_camera           = valid;
	broken_camera.camera_up = broken_camera.camera_right;
	check(!presentation(broken_camera) && !probe(broken_camera),
	      "non-orthogonal camera basis rejected");
	auto slow         = valid;
	slow.milliseconds = 100;
	check(presentation(slow), "presentation frame-time boundary accepted");
	slow.milliseconds = 101;
	check(!presentation(slow) && probe(slow),
	      "long presentation gap falls back before SDK dispatch");
	slow.milliseconds = 251;
	check(!probe(slow), "probe frame-time boundary retained");
	check(ValidateFsrPresentationFrame(valid, 0, 1080, true) != nullptr &&
	          ValidateFsrPresentationFrame(valid, 20000, 1080, true) != nullptr,
	      "invalid display dimensions rejected");

	TemporalHistory history;
	check(!history.IsDuplicate(0) && history.NeedsReset(0, 1920, 1080, false, true),
	      "first real frame resets history");
	history.Commit(10, 1920, 1080, true);
	check(history.IsDuplicate(10), "repeated real frame is a duplicate");
	check(!history.NeedsReset(11, 1920, 1080, false, true),
	      "consecutive real frame retains history");
	check(history.NeedsReset(12, 1920, 1080, false, true), "skipped real frame resets history");
	check(history.NeedsReset(9, 1920, 1080, false, true), "rewound real-frame ID resets history");
	check(history.NeedsReset(11, 1920, 1080, true, true), "camera cut explicitly resets history");
	check(history.NeedsReset(11, 1280, 720, false, true),
	      "dynamic render-size change resets history");
	// An SDK failure must invalidate continuity without consuming the failed frame.
	history.Invalidate();
	check(history.IsDuplicate(10) && !history.IsDuplicate(11),
	      "suspension remembers the last real frame without accepting a failed frame");
	check(history.NeedsReset(11, 1920, 1080, false, true), "resume after failure resets history");
	history.Commit(11, 1920, 1080, false);
	check(history.NeedsReset(12, 1920, 1080, false, true),
	      "generation resumes with reset after an overlay or pause disabled it");
	check(!history.NeedsReset(12, 1920, 1080, false, false),
	      "SR-only history remains continuous while generation is disabled");
	history.Commit(std::numeric_limits<uint64_t>::max(), 1920, 1080, true);
	check(history.NeedsReset(0, 1920, 1080, false, true),
	      "frame-ID wrap cannot preserve stale history");
	history = {};
	check(!history.IsDuplicate(0) && history.NeedsReset(0, 1920, 1080, false, true),
	      "new context discards previous history");
	std::printf("Temporal input tests: %d checks, %d failures\n", checks, failures);
	// Presentation counters keep every outcome separate and never invent generated frames.
	TemporalCounters counters;
	counters.presents            = 120;
	counters.new_frames          = 100;
	counters.redraws             = 15;
	counters.missing_guides      = 3;
	counters.timing_rejected     = 1;
	counters.record_failed       = 1;
	counters.upscaled            = 99;
	counters.generation_prepared = 98;
	counters.sdk_presents        = 230;
	const auto replacement       = counters.Format(true);
	check(replacement.find("120 presents (100 new guest frames, 15 redraws, 3 without guides, 1 "
	                       "timing-rejected, 1 failed); recorded SR 99, FG prepare 98") !=
	              std::string::npos &&
	          replacement.find("SDK swapchain presented 230 (about 110 generated)") !=
	              std::string::npos,
	      "counters report each outcome and generated presents from the SDK count");
	check(counters.Format(false).find("native swapchain, no generated presents") !=
	              std::string::npos &&
	          counters.Format(false).find("SDK swapchain") == std::string::npos,
	      "without the replacement swapchain no generated presents are claimed");
	counters.sdk_presents = 60;
	check(counters.Format(true).find("none generated") != std::string::npos &&
	          counters.Format(true).find("about") == std::string::npos,
	      "fewer SDK presents than application presents never underflow into generated frames");
	counters.Reset();
	check(counters.presents == 0 && counters.new_frames == 0 && counters.sdk_presents == 0 &&
	          counters.generation_prepared == 0,
	      "reset clears every counter");
	return failures != 0;
}
