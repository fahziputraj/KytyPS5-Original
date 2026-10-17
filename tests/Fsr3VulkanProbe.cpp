#define VK_NO_PROTOTYPES
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC    1
#define VULKAN_HPP_ENABLE_DYNAMIC_LOADER_TOOL 1
#define VULKAN_HPP_NO_EXCEPTIONS
#define VULKAN_HPP_NO_CONSTRUCTORS
#include "graphics/presentation/frameGeneration/fsr3Backend.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>
#include <vulkan/vulkan.hpp>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace {
constexpr uint32_t Width = 320, Height = 192;
constexpr size_t   Pixels = Width * Height, ColorBytes = Pixels * 4;

void Check(bool value, const char* operation) {
	if (!value) {
		std::fprintf(stderr, "FSR3 GPU probe failed: %s\n", operation);
		std::exit(1);
	}
}
void Require(vk::Result result, const char* operation) {
	if (result != vk::Result::eSuccess) {
		std::fprintf(stderr, "%s: %d\n", operation, static_cast<int>(result));
		std::exit(1);
	}
}

struct Image {
	vk::Image        handle;
	vk::DeviceMemory memory;
	vk::Format       format;
	vk::ImageLayout  layout = vk::ImageLayout::eUndefined;
};

class Device {
public:
	vk::detail::DynamicLoader loader;
	vk::Instance              instance;
	vk::PhysicalDevice        physical;
	vk::Device                device;
	vk::Queue                 queue;
	vk::CommandPool           pool;
	vk::CommandBuffer         command;
	vk::Buffer                buffer;
	vk::DeviceMemory          memory;
	void*                     mapped = nullptr;
	std::vector<Image>        images;

	~Device() {
		if (device) {
			(void)device.waitIdle();
			if (mapped) device.unmapMemory(memory);
			for (auto& image: images) {
				device.destroyImage(image.handle);
				device.freeMemory(image.memory);
			}
			if (buffer) device.destroyBuffer(buffer);
			if (memory) device.freeMemory(memory);
			if (pool) device.destroyCommandPool(pool);
			device.destroy();
		}
		if (instance) instance.destroy();
	}

	uint32_t MemoryType(uint32_t bits, vk::MemoryPropertyFlags flags) {
		const auto properties = physical.getMemoryProperties();
		for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
			if ((bits & (1u << i)) && (properties.memoryTypes[i].propertyFlags & flags) == flags)
				return i;
		}
		Check(false, "no suitable Vulkan memory type");
		return 0;
	}

	bool Initialize() {
		auto get_instance_proc =
		    loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
		if (!get_instance_proc) return false;
		VULKAN_HPP_DEFAULT_DISPATCHER.init(get_instance_proc);
		vk::ApplicationInfo app {};
		app.pApplicationName = "Kyty FSR3 offscreen GPU test";
		app.apiVersion       = VK_API_VERSION_1_3;
		vk::InstanceCreateInfo create {};
		create.pApplicationInfo = &app;
		if (vk::createInstance(&create, nullptr, &instance) != vk::Result::eSuccess) return false;
		VULKAN_HPP_DEFAULT_DISPATCHER.init(instance);
		uint32_t count = 0;
		Require(instance.enumeratePhysicalDevices(&count, nullptr), "enumerate GPUs");
		if (!count) return false;
		std::vector<vk::PhysicalDevice> devices(count);
		Require(instance.enumeratePhysicalDevices(&count, devices.data()), "enumerate GPUs");
		physical = devices[0];
		for (auto candidate: devices)
			if (candidate.getProperties().deviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
				physical = candidate;
				break;
			}
		const auto properties = physical.getProperties();
		if (properties.apiVersion < VK_API_VERSION_1_3) return false;
		std::printf("GPU: %s\n", properties.deviceName.data());
		uint32_t family_count = 0;
		physical.getQueueFamilyProperties(&family_count, nullptr);
		std::vector<vk::QueueFamilyProperties> families(family_count);
		physical.getQueueFamilyProperties(&family_count, families.data());
		uint32_t family = UINT32_MAX;
		for (uint32_t i = 0; i < family_count; ++i)
			if (families[i].queueFlags & vk::QueueFlagBits::eCompute) {
				family = i;
				break;
			}
		if (family == UINT32_MAX) return false;
		vk::PhysicalDeviceVulkan13Features features13 {};
		vk::PhysicalDeviceVulkan12Features features12 {};
		features12.pNext = &features13;
		vk::PhysicalDeviceVulkan11Features features11 {};
		features11.pNext = &features12;
		vk::PhysicalDeviceFeatures2 features {};
		features.pNext = &features11;
		physical.getFeatures2(&features);
		// The SDK selects shader variants from physical capabilities. Enable the
		// corresponding supported core features on this isolated test device.
		const float               priority = 1.0f;
		vk::DeviceQueueCreateInfo queue_create {};
		queue_create.queueFamilyIndex = family;
		queue_create.queueCount       = 1;
		queue_create.pQueuePriorities = &priority;
		vk::DeviceCreateInfo device_create {};
		device_create.pNext                = &features;
		device_create.queueCreateInfoCount = 1;
		device_create.pQueueCreateInfos    = &queue_create;
		Require(physical.createDevice(&device_create, nullptr, &device), "create GPU device");
		VULKAN_HPP_DEFAULT_DISPATCHER.init(device);
		queue = device.getQueue(family, 0);
		vk::CommandPoolCreateInfo pool_create {};
		pool_create.queueFamilyIndex = family;
		pool_create.flags            = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
		Require(device.createCommandPool(&pool_create, nullptr, &pool), "create command pool");
		vk::CommandBufferAllocateInfo command_create {};
		command_create.commandPool        = pool;
		command_create.commandBufferCount = 1;
		command_create.level              = vk::CommandBufferLevel::ePrimary;
		Require(device.allocateCommandBuffers(&command_create, &command),
		        "allocate command buffer");
		vk::BufferCreateInfo buffer_create {};
		buffer_create.size = ColorBytes * 5;
		buffer_create.usage =
		    vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst;
		Require(device.createBuffer(&buffer_create, nullptr, &buffer), "create staging buffer");
		const auto             requirements = device.getBufferMemoryRequirements(buffer);
		vk::MemoryAllocateInfo allocation {};
		allocation.allocationSize = requirements.size;
		allocation.memoryTypeIndex =
		    MemoryType(requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible |
		                                                vk::MemoryPropertyFlagBits::eHostCoherent);
		Require(device.allocateMemory(&allocation, nullptr, &memory), "allocate staging memory");
		Require(device.bindBufferMemory(buffer, memory, 0), "bind staging memory");
		Require(device.mapMemory(memory, 0, buffer_create.size, {}, &mapped), "map staging memory");
		return true;
	}

	void AddImage(vk::Format format) {
		Image image {};
		image.format = format;
		vk::ImageCreateInfo create {};
		create.imageType = vk::ImageType::e2D;
		create.extent    = {Width, Height, 1};
		create.mipLevels = create.arrayLayers = 1;
		create.format                         = format;
		create.tiling                         = vk::ImageTiling::eOptimal;
		create.samples                        = vk::SampleCountFlagBits::e1;
		create.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage |
		               vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst;
		Require(device.createImage(&create, nullptr, &image.handle), "create test image");
		const auto             requirements = device.getImageMemoryRequirements(image.handle);
		vk::MemoryAllocateInfo allocation {};
		allocation.allocationSize = requirements.size;
		allocation.memoryTypeIndex =
		    MemoryType(requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
		Require(device.allocateMemory(&allocation, nullptr, &image.memory), "allocate test image");
		Require(device.bindImageMemory(image.handle, image.memory, 0), "bind test image");
		images.push_back(image);
	}

	void Transition(Image& image, vk::ImageLayout layout) {
		vk::ImageMemoryBarrier barrier {};
		barrier.image               = image.handle;
		barrier.oldLayout           = image.layout;
		barrier.newLayout           = layout;
		barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.srcAccessMask =
		    image.layout == vk::ImageLayout::eUndefined
		        ? vk::AccessFlags {}
		        : vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite;
		barrier.dstAccessMask = vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite;
		barrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
		command.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands,
		                        vk::PipelineStageFlagBits::eAllCommands, {}, 0, nullptr, 0, nullptr,
		                        1, &barrier);
		image.layout = layout;
	}

	Libs::Graphics::FrameGeneration::FsrImage Input(size_t i) const {
		return {static_cast<VkImage>(images[i].handle), static_cast<VkFormat>(images[i].format),
		        Width, Height};
	}
};
} // namespace

int wmain(int argc, wchar_t** argv) {
	if (argc != 3) {
		std::fprintf(stderr, "Usage: fsr3_vulkan_probe <AMD SDK DLL> <output.ppm>\n");
		return 1;
	}
	Device gpu;
	if (!gpu.Initialize()) {
		std::printf("SKIP: Vulkan 1.3 GPU unavailable\n");
		return 77;
	}
	gpu.AddImage(vk::Format::eR8G8B8A8Unorm);
	gpu.AddImage(vk::Format::eR32Sfloat);
	gpu.AddImage(vk::Format::eR32G32Sfloat);
	gpu.AddImage(vk::Format::eR8G8B8A8Unorm);
	using namespace Libs::Graphics::FrameGeneration;
	Fsr3Backend backend;
	std::string error;
	const bool  initialized =
	    backend.Initialize(std::filesystem::absolute(argv[1]), static_cast<VkDevice>(gpu.device),
	                       static_cast<VkPhysicalDevice>(gpu.physical),
	                       VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceProcAddr, Width, Height,
	                       VK_FORMAT_R8G8B8A8_UNORM, error);
	Check(initialized, error.c_str());
	FsrFrame frame {};
	frame.color           = gpu.Input(0);
	frame.depth           = gpu.Input(1);
	frame.motion          = gpu.Input(2);
	frame.output          = gpu.Input(3);
	frame.milliseconds    = 1000.0f / 45.0f;
	frame.near_plane      = 0.1f;
	frame.far_plane       = 100.0f;
	frame.vertical_fov    = 1.0f;
	frame.camera_right[0] = frame.camera_up[1] = frame.camera_forward[2] = 1.0f;
	frame.view_space_to_meters                                           = 1.0f;
	frame.hudless_scene                                                  = true;
	auto invalid                                                         = frame;
	invalid.depth.image                                                  = VK_NULL_HANDLE;
	Check(!backend.Record(static_cast<VkCommandBuffer>(gpu.command), invalid, error),
	      "missing guide images must be rejected");
	auto*    bytes            = static_cast<uint8_t*>(gpu.mapped);
	auto*    depth            = reinterpret_cast<float*>(bytes + ColorBytes);
	auto*    motion           = reinterpret_cast<float*>(bytes + ColorBytes * 2);
	unsigned differing_frames = 0;
	for (uint32_t n = 0; n < 6; ++n) {
		const uint32_t left = 40 + n * 8;
		for (uint32_t y = 0; y < Height; ++y)
			for (uint32_t x = 0; x < Width; ++x) {
				const size_t p          = size_t(y) * Width + x;
				const bool   foreground = x >= left && x < left + 32 && y >= 64 && y < 128;
				bytes[p * 4]            = foreground ? 240 : 16;
				bytes[p * 4 + 1]        = foreground ? 240 : 32;
				bytes[p * 4 + 2]        = foreground ? 240 : 64;
				bytes[p * 4 + 3]        = 255;
				depth[p]                = foreground ? 0.5f : 0.9f;
				motion[p * 2]           = foreground && n != 0 ? -8.0f : 0.0f;
				motion[p * 2 + 1]       = 0.0f;
			}
		Require(gpu.command.reset({}), "reset command buffer");
		vk::CommandBufferBeginInfo begin {};
		begin.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
		Require(gpu.command.begin(&begin), "begin command buffer");
		for (size_t i = 0; i < 3; ++i) {
			gpu.Transition(gpu.images[i], vk::ImageLayout::eTransferDstOptimal);
			vk::BufferImageCopy copy {};
			copy.bufferOffset     = ColorBytes * i;
			copy.imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
			copy.imageExtent      = {Width, Height, 1};
			gpu.command.copyBufferToImage(gpu.buffer, gpu.images[i].handle,
			                              vk::ImageLayout::eTransferDstOptimal, 1, &copy);
			gpu.Transition(gpu.images[i], vk::ImageLayout::eShaderReadOnlyOptimal);
		}
		gpu.Transition(gpu.images[3], vk::ImageLayout::eGeneral);
		frame.id    = n;
		frame.reset = n == 0;
		const bool recorded =
		    backend.Record(static_cast<VkCommandBuffer>(gpu.command), frame, error);
		Check(recorded, error.c_str());
		gpu.Transition(gpu.images[3], vk::ImageLayout::eTransferSrcOptimal);
		vk::BufferImageCopy readback {};
		readback.bufferOffset     = ColorBytes * 4;
		readback.imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
		readback.imageExtent      = {Width, Height, 1};
		gpu.command.copyImageToBuffer(gpu.images[3].handle, vk::ImageLayout::eTransferSrcOptimal,
		                              gpu.buffer, 1, &readback);
		vk::MemoryBarrier host_read {};
		host_read.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
		host_read.dstAccessMask = vk::AccessFlagBits::eHostRead;
		gpu.command.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
		                            vk::PipelineStageFlagBits::eHost, {}, 1, &host_read, 0, nullptr,
		                            0, nullptr);
		Require(gpu.command.end(), "end command buffer");
		vk::SubmitInfo submit {};
		submit.commandBufferCount = 1;
		submit.pCommandBuffers    = &gpu.command;
		Require(gpu.queue.submit(1, &submit, {}), "submit SDK frame generation");
		Require(gpu.queue.waitIdle(), "wait for SDK frame generation");
		const auto* generated   = bytes + ColorBytes * 4;
		uint64_t    energy      = 0;
		size_t      differences = 0;
		for (size_t i = 0; i < ColorBytes; ++i) {
			if (i % 4 != 3) energy += generated[i];
			differences += generated[i] != bytes[i];
		}
		uint64_t white_x      = 0;
		size_t   white_pixels = 0;
		for (size_t p = 0; p < Pixels; ++p) {
			if (generated[p * 4] > 180 && generated[p * 4 + 1] > 180 &&
			    generated[p * 4 + 2] > 180) {
				white_x += p % Width;
				++white_pixels;
			}
		}
		Check(energy > Pixels * 80 && white_pixels > 1000 && white_pixels < 5000,
		      "generated image retains the scene and moving foreground");
		const double center          = static_cast<double>(white_x) / white_pixels;
		const double rendered_center = left + 15.5;
		if (n >= 2 && differences > 100 && center < rendered_center - 0.25 &&
		    center > rendered_center - 8.0)
			++differing_frames;
		std::printf("Frame %u: %zu differing bytes; generated center %.2f, rendered %.2f\n", n,
		            differences, center, rendered_center);
	}
	Check(differing_frames >= 2, "interpolation must produce distinct output after history warmup");
	std::ofstream image(std::filesystem::path(argv[2]), std::ios::binary);
	image << "P6\n" << Width << ' ' << Height << "\n255\n";
	const auto* generated = bytes + ColorBytes * 4;
	for (size_t p = 0; p < Pixels; ++p)
		image.write(reinterpret_cast<const char*>(generated + p * 4), 3);
	Check(bool(image), "save generated test image");
	backend.Shutdown();
	std::printf("PASS: six SDK GPU dispatches, generated-frame readback, missing-input rejection. "
	            "No guest/game presentation was tested.\n");
	return 0;
}
