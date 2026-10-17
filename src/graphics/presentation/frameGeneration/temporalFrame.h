#pragma once

#include "graphics/presentation/frameGeneration/sceneGuides.h"

namespace Libs::Graphics {
class CommandBuffer;
class RenderContext;
namespace FrameGeneration {

// One prepared REAL frame owns its guides until the present scheduler fence
// permits reuse. Presentation never reads the texture cache or guest memory.
class TemporalFrame final {
public:
	bool        Capture(CommandBuffer& command, const SceneGuides& candidate, vk::Extent2D extent);
	void        Release(GraphicContext& graphics);
	VulkanImage depth, motion;
	TemporalCamera camera {};
	uint64_t       id    = UINT64_MAX;
	bool           valid = false;

private:
	vk::Pipeline                 pipeline    = nullptr;
	vk::PipelineLayout           layout      = nullptr;
	vk::DescriptorSetLayout      descriptors = nullptr;
	vk::Sampler                  sampler     = nullptr;
	std::array<vk::ImageView, 2> source_views {}, target_views {};
};

} // namespace FrameGeneration
} // namespace Libs::Graphics
