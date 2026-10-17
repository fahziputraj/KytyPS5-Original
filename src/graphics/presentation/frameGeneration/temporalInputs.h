#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vulkan/vulkan.h>

namespace Libs::Graphics::FrameGeneration {

// Vulkan inputs shared by temporal backends. Descriptors do not own resources;
// callers retain the images and synchronize their access through GPU execution.
struct TemporalImage {
	VkImage  image  = VK_NULL_HANDLE;
	VkFormat format = VK_FORMAT_UNDEFINED;
	uint32_t width = 0, height = 0;
};

struct TemporalInputs {
	TemporalImage        color, depth, motion, output;
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

inline bool ValidTemporalCamera(const TemporalInputs& frame, float maximum_milliseconds) {
	const auto finite = [](const auto& vector) {
		return std::all_of(vector.begin(), vector.end(), [](float v) { return std::isfinite(v); });
	};
	const auto unit = [&](const auto& vector) {
		return finite(vector) && std::abs(vector[0] * vector[0] + vector[1] * vector[1] +
		                                  vector[2] * vector[2] - 1.0f) < 0.001f;
	};
	const auto orthogonal = [](const auto& a, const auto& b) {
		return std::abs(a[0] * b[0] + a[1] * b[1] + a[2] * b[2]) < 0.001f;
	};
	return std::isfinite(frame.milliseconds) && frame.milliseconds > 0 &&
	       frame.milliseconds <= maximum_milliseconds && std::isfinite(frame.near_plane) &&
	       std::isfinite(frame.far_plane) && frame.near_plane > 0 &&
	       frame.far_plane > frame.near_plane && std::isfinite(frame.vertical_fov) &&
	       frame.vertical_fov > 0 && frame.vertical_fov < 3.14159f &&
	       std::isfinite(frame.jitter_x) && std::isfinite(frame.jitter_y) &&
	       std::isfinite(frame.motion_scale_x) && std::isfinite(frame.motion_scale_y) &&
	       frame.motion_scale_x != 0 && frame.motion_scale_y != 0 &&
	       std::isfinite(frame.view_space_to_meters) && frame.view_space_to_meters > 0 &&
	       finite(frame.camera_position) && unit(frame.camera_right) && unit(frame.camera_up) &&
	       unit(frame.camera_forward) && orthogonal(frame.camera_right, frame.camera_up) &&
	       orthogonal(frame.camera_right, frame.camera_forward) &&
	       orthogonal(frame.camera_up, frame.camera_forward);
}

// Only successfully recorded real frames enter history. Suspension invalidates
// temporal continuity but retains the last ID so a redraw cannot become a frame.
class TemporalHistory {
public:
	bool IsDuplicate(uint64_t id) const { return m_has_frame && id == m_id; }
	bool NeedsReset(uint64_t id, uint32_t width, uint32_t height, bool reset,
	                bool generating) const {
		return reset || !m_valid || m_id == std::numeric_limits<uint64_t>::max() ||
		       id != m_id + 1 || width != m_width || height != m_height ||
		       (generating && !m_generating);
	}
	void Commit(uint64_t id, uint32_t width, uint32_t height, bool generating) {
		m_id        = id;
		m_width     = width;
		m_height    = height;
		m_has_frame = m_valid = true;
		m_generating          = generating;
	}
	void Invalidate() { m_valid = m_generating = false; }

private:
	uint64_t m_id    = 0;
	uint32_t m_width = 0, m_height = 0;
	bool     m_has_frame = false, m_valid = false, m_generating = false;
};

} // namespace Libs::Graphics::FrameGeneration
