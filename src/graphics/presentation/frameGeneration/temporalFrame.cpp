#include "graphics/presentation/frameGeneration/temporalFrame.h"

#include "gpu_blit_shaders/gpu_fsr_guides_spv.h"
#include "graphics/host_gpu/graphicContext.h"
#include "graphics/host_gpu/renderer/renderContext.h"

namespace Libs::Graphics::FrameGeneration {

void TemporalFrame::Release(GraphicContext& graphics) {
	for (auto view: source_views)
		graphics.device.destroyImageView(view, nullptr);
	for (auto view: target_views)
		graphics.device.destroyImageView(view, nullptr);
	for (auto* image: {&depth, &motion}) {
		if (image->image != nullptr) graphics.DeleteImage(*image);
		image->state = {};
		image->subresource_states.clear();
	}
	graphics.device.destroyPipeline(pipeline, nullptr);
	graphics.device.destroyPipelineLayout(layout, nullptr);
	graphics.device.destroyDescriptorSetLayout(descriptors, nullptr);
	graphics.device.destroySampler(sampler, nullptr);
	source_views = {};
	target_views = {};
	pipeline     = nullptr;
	layout       = nullptr;
	descriptors  = nullptr;
	sampler      = nullptr;
	valid        = false;
}

bool TemporalFrame::Capture(CommandBuffer& buffer, const SceneGuides& candidate,
                            vk::Extent2D extent) {
	valid = false;
	if (!candidate.camera.valid || !candidate.motion || !candidate.depth || extent.width == 0 ||
	    extent.height == 0 || extent.width > candidate.extent.width ||
	    extent.height > candidate.extent.height)
		return false;
	auto& context    = buffer.GetContext();
	auto& graphics   = context.GetGraphics();
	auto& cache      = context.GetTextureCache();
	auto* src_motion = cache.TryGetImage(candidate.motion);
	auto* src_depth  = cache.TryGetImage(candidate.depth);
	if (src_motion == nullptr || src_depth == nullptr) return false;
	for (const auto* image: {src_motion, src_depth}) {
		if (!image->registered || image->binding.needs_rebind || image->backing.layers != 1 ||
		    image->backing.samples != 1 || image->backing.extent.width != candidate.extent.width ||
		    image->backing.extent.height != candidate.extent.height ||
		    !(image->backing.usage & vk::ImageUsageFlagBits::eSampled))
			return false;
	}
	if (src_motion->backing.format != vk::Format::eR16G16B16A16Sfloat ||
	    src_depth->backing.format != vk::Format::eD32SfloatS8Uint)
		return false;
	if (depth.image != nullptr &&
	    (depth.extent.width != extent.width || depth.extent.height != extent.height))
		Release(graphics);
	const auto device = graphics.device;
	if (layout == nullptr) {
		std::array<vk::DescriptorSetLayoutBinding, 4> bindings {};
		for (uint32_t i = 0; i < bindings.size(); ++i) {
			bindings[i] = {i,
			               i < 2 ? vk::DescriptorType::eCombinedImageSampler
			                     : vk::DescriptorType::eStorageImage,
			               1, vk::ShaderStageFlagBits::eCompute};
		}
		vk::DescriptorSetLayoutCreateInfo set {};
		set.flags        = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR;
		set.bindingCount = static_cast<uint32_t>(bindings.size());
		set.pBindings    = bindings.data();
		RequireVulkanSuccess(device.createDescriptorSetLayout(&set, nullptr, &descriptors),
		                     "FSR guide descriptors");
		const vk::PushConstantRange  push {vk::ShaderStageFlagBits::eCompute, 0, 92};
		vk::PipelineLayoutCreateInfo create {};
		create.setLayoutCount         = 1;
		create.pSetLayouts            = &descriptors;
		create.pushConstantRangeCount = 1;
		create.pPushConstantRanges    = &push;
		RequireVulkanSuccess(device.createPipelineLayout(&create, nullptr, &layout),
		                     "FSR guide layout");
		vk::SamplerCreateInfo sampling {};
		sampling.magFilter = sampling.minFilter = vk::Filter::eNearest;
		sampling.mipmapMode                     = vk::SamplerMipmapMode::eNearest;
		sampling.addressModeU = sampling.addressModeV = sampling.addressModeW =
		    vk::SamplerAddressMode::eClampToEdge;
		RequireVulkanSuccess(device.createSampler(&sampling, nullptr, &sampler),
		                     "FSR guide sampler");
		vk::ShaderModuleCreateInfo shader {};
		shader.codeSize = sizeof(GPU_FSR_GUIDES_SPV);
		shader.pCode    = GPU_FSR_GUIDES_SPV;
		vk::ShaderModule module;
		RequireVulkanSuccess(device.createShaderModule(&shader, nullptr, &module),
		                     "FSR guide shader");
		vk::ComputePipelineCreateInfo compute {};
		compute.layout       = layout;
		compute.stage.stage  = vk::ShaderStageFlagBits::eCompute;
		compute.stage.module = module;
		compute.stage.pName  = "main";
		const auto result    = device.createComputePipeline(nullptr, compute);
		device.destroyShaderModule(module, nullptr);
		RequireVulkanSuccess(result.result, "FSR guide pipeline");
		pipeline = result.value;
	}
	std::array<VulkanImage*, 2> targets {&motion, &depth};
	std::array<Image*, 2>       sources {src_motion, src_depth};
	buffer.EndRendering();
	const auto                             command = buffer.Handle();
	std::array<vk::DescriptorImageInfo, 4> images {};
	for (uint32_t i = 0; i < 2; ++i) {
		auto& image = *targets[i];
		if (image.image == nullptr) {
			vk::ImageCreateInfo create {};
			create.imageType = vk::ImageType::e2D;
			create.extent    = {extent.width, extent.height, 1};
			create.mipLevels = create.arrayLayers = 1;
			create.samples                        = vk::SampleCountFlagBits::e1;
			create.format = i == 0 ? vk::Format::eR16G16Sfloat : vk::Format::eR32Sfloat;
			create.usage  = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled;
			if (!graphics.CreateImage(create, image)) {
				Release(graphics);
				return false;
			}
			vk::ImageViewCreateInfo view {};
			view.image            = image.image;
			view.viewType         = vk::ImageViewType::e2D;
			view.format           = image.format;
			view.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
			RequireVulkanSuccess(device.createImageView(&view, nullptr, &target_views[i]),
			                     "FSR guide target view");
		}
		device.destroyImageView(source_views[i], nullptr);
		vk::ImageViewCreateInfo view {};
		view.image            = sources[i]->backing.image;
		view.viewType         = vk::ImageViewType::e2D;
		view.format           = sources[i]->backing.format;
		view.subresourceRange = {
		    i == 0 ? vk::ImageAspectFlagBits::eColor : vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1};
		RequireVulkanSuccess(device.createImageView(&view, nullptr, &source_views[i]),
		                     "FSR guide source view");
		sources[i]->Transit(vk::ImageLayout::eShaderReadOnlyOptimal,
		                    vk::AccessFlagBits2::eShaderRead, {}, command);
		vk::ImageMemoryBarrier barrier {};
		barrier.srcAccessMask = vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite;
		barrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite;
		barrier.oldLayout     = vk::ImageLayout::eUndefined;
		barrier.newLayout     = vk::ImageLayout::eGeneral;
		barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image                                             = image.image;
		barrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
		command.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands,
		                        vk::PipelineStageFlagBits::eComputeShader, {}, 0, nullptr, 0,
		                        nullptr, 1, &barrier);
		images[i]     = {sampler, source_views[i], vk::ImageLayout::eShaderReadOnlyOptimal};
		images[i + 2] = {nullptr, target_views[i], vk::ImageLayout::eGeneral};
	}
	std::array<vk::WriteDescriptorSet, 4> writes {};
	for (uint32_t i = 0; i < writes.size(); ++i) {
		writes[i].dstBinding      = i;
		writes[i].descriptorCount = 1;
		writes[i].descriptorType =
		    i < 2 ? vk::DescriptorType::eCombinedImageSampler : vk::DescriptorType::eStorageImage;
		writes[i].pImageInfo = &images[i];
	}
	struct Constants {
		CameraMatrix            previous;
		std::array<float, 2>    delta, jitter;
		std::array<uint32_t, 2> size;
		uint32_t                reconstruct;
	};
	static_assert(sizeof(Constants) == 92);
	const Constants constants {candidate.camera.clip_to_previous,
	                           candidate.camera.motion_jitter_delta,
	                           candidate.camera.jitter_uv,
	                           {extent.width, extent.height},
	                           candidate.camera.reset ? 0u : 1u};
	buffer.BindPipeline(vk::PipelineBindPoint::eCompute, pipeline);
	buffer.PushDescriptors(vk::PipelineBindPoint::eCompute, layout, 0,
	                       static_cast<uint32_t>(writes.size()), writes.data());
	buffer.PushConstants(layout, vk::ShaderStageFlagBits::eCompute, sizeof(constants), &constants);
	command.dispatch((extent.width + 7) / 8, (extent.height + 7) / 8, 1);
	std::array<vk::ImageMemoryBarrier, 2> barriers {};
	for (uint32_t i = 0; i < barriers.size(); ++i) {
		auto& barrier               = barriers[i];
		barrier.srcAccessMask       = vk::AccessFlagBits::eShaderWrite;
		barrier.dstAccessMask       = vk::AccessFlagBits::eShaderRead;
		barrier.oldLayout           = vk::ImageLayout::eGeneral;
		barrier.newLayout           = vk::ImageLayout::eShaderReadOnlyOptimal;
		barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image                                             = targets[i]->image;
		barrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
	}
	command.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
	                        vk::PipelineStageFlagBits::eAllCommands, {}, 0, nullptr, 0, nullptr,
	                        static_cast<uint32_t>(barriers.size()), barriers.data());
	camera = candidate.camera;
	id     = candidate.frame;
	valid  = true;
	return true;
}

} // namespace Libs::Graphics::FrameGeneration
