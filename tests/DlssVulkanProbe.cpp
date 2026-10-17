// DLSS Super Resolution offscreen Vulkan probe: a hardware gate, not a CPU test.
//
// Runs NVIDIA's NGX runtime through DlssBackend on synthetic HUD-free frames, submits the
// work, reads the output back and checks it. Exit codes: 0 PASS, 77 SKIP (no NVIDIA Vulkan
// GPU or no feature runtime; nothing NGX-related was called), 1 FAIL.
//
//   dlss_vulkan_probe <feature dir with nvngx_dlss.dll> <data dir> <output.ppm> [--validation]
//   dlss_vulkan_probe --runtime-info <nvngx_dlss.dll>
//
// The feature directory must hold NVIDIA's official signed nvngx_dlss.dll; the probe records
// its SHA-256, file version and signer and refuses an unsigned or non-NVIDIA file. The project
// ID below is this repository's own GUID for the prototype, not an NVIDIA-issued application
// ID and not another product's. Checks:
// - several consecutive moving frames: no sentinel left in the output, the background kept,
//   the moving square at its upscaled position and size;
// - a camera cut with reset: no ghost at the old position;
// - duplicate-frame rejection without GPU work, a frame-ID gap, a discarded successful
//   recording followed by reinitialization, and a second resolution/quality configuration;
// - with --validation, zero Vulkan validation-layer errors.
// This establishes offscreen SDK execution only: not game inputs, presentation, quality or
// performance in a game.

#define VK_NO_PROTOTYPES
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC    1
#define VULKAN_HPP_ENABLE_DYNAMIC_LOADER_TOOL 1
#define VULKAN_HPP_NO_EXCEPTIONS
#define VULKAN_HPP_NO_CONSTRUCTORS
#include "graphics/presentation/frameGeneration/dlssNgx.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <vulkan/vulkan.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// Windows headers after windows.h.
#include <bcrypt.h>
#include <softpub.h>
#include <wincrypt.h>
#include <wintrust.h>
// windows.h maps MemoryBarrier to an intrinsic, which would rename vk::MemoryBarrier.
#undef MemoryBarrier

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

using namespace Libs::Graphics::FrameGeneration;

namespace {

constexpr int      Pass = 0, Fail = 1, Skip = 77;
constexpr char     ProjectId[]     = "624b3d1a-e585-43fa-a5d9-19fb002c698e";
constexpr char     EngineVersion[] = "kyty-dlss-probe-1";
constexpr uint32_t NvidiaVendor    = 0x10de;

[[noreturn]] void Exit(int code, const std::string& text) {
	std::fprintf(code == Pass ? stdout : stderr, "%s: %s\n",
	             code == Pass   ? "PASS"
	             : code == Skip ? "SKIP"
	                            : "FAIL",
	             text.c_str());
	std::exit(code);
}
void Check(bool value, const std::string& text) {
	if (!value) Exit(Fail, text);
}
void Require(vk::Result result, const char* operation) {
	if (result != vk::Result::eSuccess)
		Exit(Fail, std::string(operation) + " returned " + vk::to_string(result));
}

std::string Narrow(const std::wstring& text) {
	std::string out;
	for (const auto c: text)
		out += c < 128 ? static_cast<char>(c) : '?';
	return out;
}

struct RuntimeInfo {
	std::string sha256, version, signer;
	bool        signed_valid = false;
};

std::string Sha256(const std::filesystem::path& path) {
	std::ifstream file(path, std::ios::binary);
	Check(bool(file), "cannot read " + path.string());
	std::vector<unsigned char>    data((std::istreambuf_iterator<char>(file)), {});
	std::array<unsigned char, 32> hash {};
	Check(BCRYPT_SUCCESS(BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, data.data(),
	                                static_cast<ULONG>(data.size()), hash.data(),
	                                static_cast<ULONG>(hash.size()))),
	      "SHA-256 of the feature runtime");
	std::string text;
	char        digit[3];
	for (const auto byte: hash) {
		std::snprintf(digit, sizeof(digit), "%02X", byte);
		text += digit;
	}
	return text;
}

std::string FileVersion(const std::filesystem::path& path) {
	DWORD      handle = 0;
	const auto size   = GetFileVersionInfoSizeW(path.c_str(), &handle);
	if (size == 0) return "unknown";
	std::vector<unsigned char> data(size);
	VS_FIXEDFILEINFO*          info   = nullptr;
	UINT                       length = 0;
	if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data()) ||
	    !VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &length) || !info)
		return "unknown";
	return std::to_string(HIWORD(info->dwFileVersionMS)) + "." +
	       std::to_string(LOWORD(info->dwFileVersionMS)) + "." +
	       std::to_string(HIWORD(info->dwFileVersionLS)) + "." +
	       std::to_string(LOWORD(info->dwFileVersionLS));
}

// Authenticode verification of the embedded signature, and the signing certificate's name.
void VerifySignature(const std::filesystem::path& path, RuntimeInfo& info) {
	WINTRUST_FILE_INFO file {};
	file.cbStruct        = sizeof(file);
	file.pcwszFilePath   = path.c_str();
	GUID          action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
	WINTRUST_DATA trust {};
	trust.cbStruct            = sizeof(trust);
	trust.dwUIChoice          = WTD_UI_NONE;
	trust.fdwRevocationChecks = WTD_REVOKE_NONE;
	trust.dwUnionChoice       = WTD_CHOICE_FILE;
	trust.pFile               = &file;
	trust.dwStateAction       = WTD_STATEACTION_VERIFY;
	info.signed_valid =
	    WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &trust) == 0;
	if (trust.hWVTStateData) {
		if (auto* provider = WTHelperProvDataFromStateData(trust.hWVTStateData)) {
			if (auto* signer = WTHelperGetProvSignerFromChain(provider, 0, FALSE, 0);
			    signer && signer->pasCertChain && signer->csCertChain > 0) {
				std::array<wchar_t, 256> name {};
				CertGetNameStringW(signer->pasCertChain[0].pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0,
				                   nullptr, name.data(), static_cast<DWORD>(name.size()));
				info.signer = Narrow(name.data());
			}
		}
	}
	trust.dwStateAction = WTD_STATEACTION_CLOSE;
	WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &trust);
}

RuntimeInfo InspectRuntime(const std::filesystem::path& path) {
	RuntimeInfo info;
	info.sha256  = Sha256(path);
	info.version = FileVersion(path);
	VerifySignature(path, info);
	std::printf("RUNTIME %s: sha256 %s, version %s, signature %s, signer \"%s\"\n",
	            path.string().c_str(), info.sha256.c_str(), info.version.c_str(),
	            info.signed_valid ? "valid" : "INVALID", info.signer.c_str());
	return info;
}

uint16_t Half(float value) {
	uint32_t bits = 0;
	std::memcpy(&bits, &value, sizeof(bits));
	const uint32_t sign     = (bits >> 16u) & 0x8000u;
	int32_t        exponent = static_cast<int32_t>((bits >> 23u) & 0xffu) - 127 + 15;
	uint32_t       mantissa = bits & 0x7fffffu;
	if (exponent <= 0) return static_cast<uint16_t>(sign);
	if (exponent >= 31) return static_cast<uint16_t>(sign | 0x7c00u);
	return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exponent) << 10u) |
	                             (mantissa >> 13u));
}

float Halton(uint32_t index, uint32_t base) {
	float fraction = 1.0f, result = 0.0f;
	for (; index > 0; index /= base) {
		fraction /= static_cast<float>(base);
		result += fraction * static_cast<float>(index % base);
	}
	return result;
}

uint32_t                       ErrorMessages = 0;
VKAPI_ATTR VkBool32 VKAPI_CALL OnValidation(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                            VkDebugUtilsMessageTypeFlagsEXT,
                                            const VkDebugUtilsMessengerCallbackDataEXT* data,
                                            void*) {
	if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
		if (ErrorMessages++ < 10) std::fprintf(stderr, "VALIDATION: %s\n", data->pMessage);
	}
	return VK_FALSE;
}

struct Image {
	vk::Image           handle;
	vk::DeviceMemory    memory;
	vk::ImageView       view;
	vk::Format          format = vk::Format::eUndefined;
	uint32_t            width = 0, height = 0;
	vk::ImageUsageFlags usage;
	vk::ImageLayout     layout = vk::ImageLayout::eUndefined;
};

class Gpu {
public:
	vk::detail::DynamicLoader    loader;
	PFN_vkGetInstanceProcAddr    get_instance_proc = nullptr;
	vk::Instance                 instance;
	vk::DebugUtilsMessengerEXT   messenger;
	vk::PhysicalDevice           physical;
	vk::PhysicalDeviceProperties properties {};
	vk::Device                   device;
	vk::Queue                    queue;
	vk::CommandPool              pool;
	vk::CommandBuffer            command;
	vk::Fence                    fence;
	vk::Buffer                   buffer;
	vk::DeviceMemory             memory;
	uint8_t*                     mapped = nullptr;
	std::vector<std::string>     instance_extensions, device_extensions;

	~Gpu() {
		if (device) {
			(void)device.waitIdle();
			if (mapped) device.unmapMemory(memory);
			if (buffer) device.destroyBuffer(buffer);
			if (memory) device.freeMemory(memory);
			if (fence) device.destroyFence(fence);
			if (pool) device.destroyCommandPool(pool);
			device.destroy();
		}
		if (messenger) instance.destroyDebugUtilsMessengerEXT(messenger);
		if (instance) instance.destroy();
	}

	bool LoadLoader() {
		get_instance_proc =
		    loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
		if (!get_instance_proc) return false;
		VULKAN_HPP_DEFAULT_DISPATCHER.init(get_instance_proc);
		return true;
	}

	// A throwaway instance only to look for an NVIDIA GPU before any NGX call.
	static bool HasNvidiaGpu(std::string& name) {
		vk::ApplicationInfo app {};
		app.apiVersion = VK_API_VERSION_1_3;
		vk::InstanceCreateInfo create {};
		create.pApplicationInfo = &app;
		vk::Instance probe;
		if (vk::createInstance(&create, nullptr, &probe) != vk::Result::eSuccess) return false;
		VULKAN_HPP_DEFAULT_DISPATCHER.init(probe);
		uint32_t count = 0;
		(void)probe.enumeratePhysicalDevices(&count, nullptr);
		std::vector<vk::PhysicalDevice> devices(count);
		(void)probe.enumeratePhysicalDevices(&count, devices.data());
		bool found = false;
		for (const auto candidate: devices) {
			const auto properties = candidate.getProperties();
			name += std::string(name.empty() ? "" : ", ") + properties.deviceName.data();
			found = found || properties.vendorID == NvidiaVendor;
		}
		probe.destroy();
		return found;
	}

	void CreateInstance(const std::vector<std::string>& required, bool validation) {
		std::vector<const char*> extensions;
		for (const auto& name: required)
			extensions.push_back(name.c_str());
		std::vector<const char*> layers;
		if (validation) {
			uint32_t count = 0;
			(void)vk::enumerateInstanceLayerProperties(&count, nullptr);
			std::vector<vk::LayerProperties> available(count);
			(void)vk::enumerateInstanceLayerProperties(&count, available.data());
			bool present = false;
			for (const auto& layer: available)
				present =
				    present || std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0;
			Check(present, "--validation needs VK_LAYER_KHRONOS_validation (Vulkan SDK) installed");
			layers.push_back("VK_LAYER_KHRONOS_validation");
			extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		}
		vk::ApplicationInfo app {};
		app.pApplicationName = "Kyty DLSS SR offscreen probe";
		app.apiVersion       = VK_API_VERSION_1_3;
		vk::InstanceCreateInfo create {};
		create.pApplicationInfo        = &app;
		create.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());
		create.ppEnabledExtensionNames = extensions.data();
		create.enabledLayerCount       = static_cast<uint32_t>(layers.size());
		create.ppEnabledLayerNames     = layers.data();
		Require(vk::createInstance(&create, nullptr, &instance),
		        "vkCreateInstance with NGX extensions");
		VULKAN_HPP_DEFAULT_DISPATCHER.init(instance);
		for (const auto* name: extensions)
			instance_extensions.emplace_back(name);
		if (validation) {
			vk::DebugUtilsMessengerCreateInfoEXT messenger_create {};
			messenger_create.messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eError |
			                                   vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning;
			messenger_create.messageType     = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
			                                   vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
			                                   vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
			messenger_create.pfnUserCallback =
			    reinterpret_cast<vk::PFN_DebugUtilsMessengerCallbackEXT>(OnValidation);
			Require(instance.createDebugUtilsMessengerEXT(&messenger_create, nullptr, &messenger),
			        "create validation messenger");
		}
		uint32_t count = 0;
		Require(instance.enumeratePhysicalDevices(&count, nullptr), "enumerate GPUs");
		std::vector<vk::PhysicalDevice> devices(count);
		Require(instance.enumeratePhysicalDevices(&count, devices.data()), "enumerate GPUs");
		for (const auto candidate: devices) {
			if (candidate.getProperties().vendorID == NvidiaVendor) {
				physical = candidate;
				break;
			}
		}
		Check(bool(physical), "the NVIDIA GPU disappeared after instance creation");
		properties = physical.getProperties();
		Check(properties.apiVersion >= VK_API_VERSION_1_3,
		      "the NVIDIA Vulkan driver is older than 1.3");
	}

	void CreateDevice(const std::vector<std::string>& required) {
		uint32_t family_count = 0;
		physical.getQueueFamilyProperties(&family_count, nullptr);
		std::vector<vk::QueueFamilyProperties> families(family_count);
		physical.getQueueFamilyProperties(&family_count, families.data());
		uint32_t family = UINT32_MAX;
		for (uint32_t i = 0; i < family_count && family == UINT32_MAX; ++i)
			if (families[i].queueFlags & vk::QueueFlagBits::eGraphics &&
			    families[i].queueFlags & vk::QueueFlagBits::eCompute)
				family = i;
		Check(family != UINT32_MAX, "no graphics+compute queue family");
		// Enable every supported core feature on this isolated device, so whatever the runtime
		// needs beyond its reported extensions is available.
		vk::PhysicalDeviceVulkan13Features features13 {};
		vk::PhysicalDeviceVulkan12Features features12 {};
		features12.pNext = &features13;
		vk::PhysicalDeviceVulkan11Features features11 {};
		features11.pNext = &features12;
		vk::PhysicalDeviceFeatures2 features {};
		features.pNext = &features11;
		physical.getFeatures2(&features);
		std::vector<const char*> extensions;
		for (const auto& name: required)
			extensions.push_back(name.c_str());
		const float               priority = 1.0f;
		vk::DeviceQueueCreateInfo queue_create {};
		queue_create.queueFamilyIndex = family;
		queue_create.queueCount       = 1;
		queue_create.pQueuePriorities = &priority;
		vk::DeviceCreateInfo create {};
		create.pNext                   = &features;
		create.queueCreateInfoCount    = 1;
		create.pQueueCreateInfos       = &queue_create;
		create.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());
		create.ppEnabledExtensionNames = extensions.data();
		Require(physical.createDevice(&create, nullptr, &device),
		        "vkCreateDevice with NGX extensions");
		VULKAN_HPP_DEFAULT_DISPATCHER.init(device);
		device_extensions = required;
		queue             = device.getQueue(family, 0);
		vk::CommandPoolCreateInfo pool_create {};
		pool_create.queueFamilyIndex = family;
		pool_create.flags            = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
		Require(device.createCommandPool(&pool_create, nullptr, &pool), "create command pool");
		vk::CommandBufferAllocateInfo allocate {};
		allocate.commandPool        = pool;
		allocate.commandBufferCount = 1;
		allocate.level              = vk::CommandBufferLevel::ePrimary;
		Require(device.allocateCommandBuffers(&allocate, &command), "allocate command buffer");
		vk::FenceCreateInfo fence_create {};
		Require(device.createFence(&fence_create, nullptr, &fence), "create fence");
		// Staging: three render-size inputs and one output, at most 1440x810 RGBA8.
		vk::BufferCreateInfo buffer_create {};
		buffer_create.size = 3ull * 960 * 540 * 4 + 1440ull * 810 * 4;
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
		void* pointer = nullptr;
		Require(device.mapMemory(memory, 0, buffer_create.size, {}, &pointer),
		        "map staging memory");
		mapped = static_cast<uint8_t*>(pointer);
	}

	uint32_t MemoryType(uint32_t bits, vk::MemoryPropertyFlags flags) const {
		const auto memory_properties = physical.getMemoryProperties();
		for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i)
			if ((bits & (1u << i)) &&
			    (memory_properties.memoryTypes[i].propertyFlags & flags) == flags)
				return i;
		Exit(Fail, "no suitable Vulkan memory type");
	}

	Image CreateImage(vk::Format format, uint32_t width, uint32_t height,
	                  vk::ImageUsageFlags usage) {
		Image image;
		image.format = format;
		image.width  = width;
		image.height = height;
		image.usage  = usage;
		vk::ImageCreateInfo create {};
		create.imageType   = vk::ImageType::e2D;
		create.extent      = vk::Extent3D {width, height, 1};
		create.mipLevels   = 1;
		create.arrayLayers = 1;
		create.format      = format;
		create.tiling      = vk::ImageTiling::eOptimal;
		create.samples     = vk::SampleCountFlagBits::e1;
		create.usage       = usage;
		Require(device.createImage(&create, nullptr, &image.handle), "create probe image");
		const auto             requirements = device.getImageMemoryRequirements(image.handle);
		vk::MemoryAllocateInfo allocation {};
		allocation.allocationSize = requirements.size;
		allocation.memoryTypeIndex =
		    MemoryType(requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
		Require(device.allocateMemory(&allocation, nullptr, &image.memory), "allocate probe image");
		Require(device.bindImageMemory(image.handle, image.memory, 0), "bind probe image");
		vk::ImageViewCreateInfo view {};
		view.image    = image.handle;
		view.viewType = vk::ImageViewType::e2D;
		view.format   = format;
		view.subresourceRange =
		    vk::ImageSubresourceRange {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
		Require(device.createImageView(&view, nullptr, &image.view), "create probe image view");
		return image;
	}

	void Destroy(Image& image) {
		if (image.view) device.destroyImageView(image.view);
		if (image.handle) device.destroyImage(image.handle);
		if (image.memory) device.freeMemory(image.memory);
		image = {};
	}

	void Transition(Image& image, vk::ImageLayout layout) {
		vk::ImageMemoryBarrier barrier {};
		barrier.image               = image.handle;
		barrier.oldLayout           = image.layout;
		barrier.newLayout           = layout;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.srcAccessMask =
		    image.layout == vk::ImageLayout::eUndefined
		        ? vk::AccessFlags {}
		        : vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite;
		barrier.dstAccessMask = vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite;
		barrier.subresourceRange =
		    vk::ImageSubresourceRange {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
		command.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands,
		                        vk::PipelineStageFlagBits::eAllCommands, {}, 0, nullptr, 0, nullptr,
		                        1, &barrier);
		image.layout = layout;
	}

	void Begin() {
		Require(command.reset({}), "reset command buffer");
		vk::CommandBufferBeginInfo begin {};
		begin.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
		Require(command.begin(&begin), "begin command buffer");
	}

	void SubmitAndWait() {
		Require(command.end(), "end command buffer");
		vk::SubmitInfo submit {};
		submit.commandBufferCount = 1;
		submit.pCommandBuffers    = &command;
		Require(device.resetFences(1, &fence), "reset fence");
		Require(queue.submit(1, &submit, fence), "submit DLSS frame");
		Require(device.waitForFences(1, &fence, VK_TRUE, 10'000'000'000ull), "wait for DLSS frame");
	}
};

// One SR configuration: render-size inputs and an output image.
struct Scene {
	DlssOptions options;
	Image       color, depth, motion, output;
};

constexpr uint32_t               Square = 48; // render pixels
constexpr std::array<uint8_t, 4> Background {24, 48, 96, 255};
constexpr std::array<uint8_t, 4> Sentinel {255, 0, 255, 255};

struct Readback {
	double center_x = 0, center_y = 0;
	size_t bright = 0, sentinel = 0;
	double corner_error        = 0;
	double old_area_brightness = 0;
};

class Probe {
public:
	Gpu&            gpu;
	DlssBackend     backend;
	Scene           scene;
	DlssApplication application;
	DlssDevice      device;

	Probe(Gpu& g, const DlssApplication& app): gpu(g), application(app) {
		device.instance            = static_cast<VkInstance>(gpu.instance);
		device.physical            = static_cast<VkPhysicalDevice>(gpu.physical);
		device.device              = static_cast<VkDevice>(gpu.device);
		device.get_instance_proc   = gpu.get_instance_proc;
		device.get_device_proc     = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceProcAddr;
		device.vendor_id           = gpu.properties.vendorID;
		device.instance_extensions = gpu.instance_extensions;
		device.device_extensions   = gpu.device_extensions;
	}

	void Configure(const DlssOptions& options) {
		(void)gpu.device.waitIdle();
		backend.Shutdown();
		for (auto* image: {&scene.color, &scene.depth, &scene.motion, &scene.output})
			gpu.Destroy(*image);
		scene.options    = options;
		const auto input = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
		scene.color      = gpu.CreateImage(vk::Format::eR8G8B8A8Unorm, options.render_width,
		                                   options.render_height, input);
		scene.depth      = gpu.CreateImage(vk::Format::eR32Sfloat, options.render_width,
		                                   options.render_height, input);
		scene.motion     = gpu.CreateImage(vk::Format::eR16G16Sfloat, options.render_width,
		                                   options.render_height, input);
		scene.output     = gpu.CreateImage(
		    vk::Format::eR8G8B8A8Unorm, options.output_width, options.output_height,
		    vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc |
		        vk::ImageUsageFlagBits::eTransferDst);
		Initialize();
	}

	void Initialize() {
		std::string error;
		Check(backend.Initialize(application, device, scene.options, CreateDlssNgxSdk(), error),
		      "DLSS initialization: " + error);
	}

	static DlssBinding Binding(const Image& image, vk::ImageLayout layout) {
		DlssBinding binding;
		binding.view   = static_cast<VkImageView>(image.view);
		binding.layout = static_cast<VkImageLayout>(layout);
		binding.usage  = static_cast<VkImageUsageFlags>(image.usage);
		return binding;
	}

	DlssFrame Frame(uint64_t id, uint32_t index, bool cut) const {
		DlssFrame  frame;
		auto&      t     = frame.temporal;
		const auto image = [](const Image& value) {
			return TemporalImage {static_cast<VkImage>(value.handle),
			                      static_cast<VkFormat>(value.format), value.width, value.height};
		};
		t.color                = image(scene.color);
		t.depth                = image(scene.depth);
		t.motion               = image(scene.motion);
		t.output               = image(scene.output);
		t.id                   = id;
		t.reset                = cut;
		t.milliseconds         = 1000.0f / 60.0f;
		t.near_plane           = 0.1f;
		t.far_plane            = 100.0f;
		t.vertical_fov         = 1.0f;
		t.view_space_to_meters = 1.0f;
		t.camera_right         = {1, 0, 0};
		t.camera_up            = {0, 1, 0};
		t.camera_forward       = {0, 0, 1};
		t.motion_scale_x = t.motion_scale_y = 1.0f; // motion is written in render pixels
		t.jitter_x                          = Halton(index % 8 + 1, 2) - 0.5f;
		t.jitter_y                          = Halton(index % 8 + 1, 3) - 0.5f;
		t.hudless_scene                     = true;
		frame.color  = Binding(scene.color, vk::ImageLayout::eShaderReadOnlyOptimal);
		frame.depth  = Binding(scene.depth, vk::ImageLayout::eShaderReadOnlyOptimal);
		frame.motion = Binding(scene.motion, vk::ImageLayout::eShaderReadOnlyOptimal);
		frame.output = Binding(scene.output, vk::ImageLayout::eGeneral);
		return frame;
	}

	// The square's render-space left edge for a frame; motion is `step` pixels per frame.
	// Rasterizes the jittered sample positions and writes current-to-previous motion.
	void Upload(const DlssFrame& frame, float left, float top, float step) {
		const auto w = scene.options.render_width, h = scene.options.render_height;
		auto*      color  = gpu.mapped;
		auto*      depth  = reinterpret_cast<float*>(gpu.mapped + size_t(w) * h * 4);
		auto*      motion = reinterpret_cast<uint16_t*>(gpu.mapped + size_t(w) * h * 8);
		for (uint32_t y = 0; y < h; ++y) {
			for (uint32_t x = 0; x < w; ++x) {
				const float sx = static_cast<float>(x) + 0.5f + frame.temporal.jitter_x;
				const float sy = static_cast<float>(y) + 0.5f + frame.temporal.jitter_y;
				const bool  inside =
				    sx >= left && sx < left + Square && sy >= top && sy < top + Square;
				const size_t p = size_t(y) * w + x;
				for (int c = 0; c < 4; ++c)
					color[p * 4 + c] = c == 3 ? 255 : inside ? 240 : Background[c];
				depth[p]          = inside ? 0.3f : 0.9f;
				motion[p * 2]     = Half(inside ? -step : 0.0f);
				motion[p * 2 + 1] = Half(0.0f);
			}
		}
	}

	void RecordUploads() {
		const auto            w = scene.options.render_width, h = scene.options.render_height;
		std::array<Image*, 3> inputs {&scene.color, &scene.depth, &scene.motion};
		for (size_t i = 0; i < inputs.size(); ++i) {
			gpu.Transition(*inputs[i], vk::ImageLayout::eTransferDstOptimal);
			vk::BufferImageCopy copy {};
			copy.bufferOffset = size_t(w) * h * 4 * i;
			copy.imageSubresource =
			    vk::ImageSubresourceLayers {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
			copy.imageExtent = vk::Extent3D {w, h, 1};
			gpu.command.copyBufferToImage(gpu.buffer, inputs[i]->handle,
			                              vk::ImageLayout::eTransferDstOptimal, 1, &copy);
			gpu.Transition(*inputs[i], vk::ImageLayout::eShaderReadOnlyOptimal);
		}
		// The sentinel shows any output pixel DLSS did not write.
		gpu.Transition(scene.output, vk::ImageLayout::eGeneral);
		vk::ClearColorValue sentinel {};
		sentinel.float32[0] = 1.0f;
		sentinel.float32[1] = 0.0f;
		sentinel.float32[2] = 1.0f;
		sentinel.float32[3] = 1.0f;
		const vk::ImageSubresourceRange range {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
		gpu.command.clearColorImage(scene.output.handle, vk::ImageLayout::eGeneral, &sentinel, 1,
		                            &range);
		gpu.Transition(scene.output, vk::ImageLayout::eGeneral);
	}

	size_t OutputOffset() const {
		return size_t(scene.options.render_width) * scene.options.render_height * 12;
	}

	void RecordReadback() {
		gpu.Transition(scene.output, vk::ImageLayout::eTransferSrcOptimal);
		vk::BufferImageCopy copy {};
		copy.bufferOffset = OutputOffset();
		copy.imageSubresource =
		    vk::ImageSubresourceLayers {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
		copy.imageExtent =
		    vk::Extent3D {scene.options.output_width, scene.options.output_height, 1};
		gpu.command.copyImageToBuffer(scene.output.handle, vk::ImageLayout::eTransferSrcOptimal,
		                              gpu.buffer, 1, &copy);
		vk::MemoryBarrier host {};
		host.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
		host.dstAccessMask = vk::AccessFlagBits::eHostRead;
		gpu.command.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
		                            vk::PipelineStageFlagBits::eHost, {}, 1, &host, 0, nullptr, 0,
		                            nullptr);
	}

	Readback Analyze(float old_left, float old_top) const {
		Readback     result;
		const auto   ow = scene.options.output_width, oh = scene.options.output_height;
		const double scale = static_cast<double>(ow) / scene.options.render_width;
		const auto*  out   = gpu.mapped + OutputOffset();
		double       sx = 0, sy = 0, old_sum = 0;
		size_t       old_count = 0;
		for (uint32_t y = 0; y < oh; ++y) {
			for (uint32_t x = 0; x < ow; ++x) {
				const auto* p = out + (size_t(y) * ow + x) * 4;
				if (p[0] == Sentinel[0] && p[1] == Sentinel[1] && p[2] == Sentinel[2])
					++result.sentinel;
				if (p[0] > 180 && p[1] > 180 && p[2] > 180) {
					sx += x;
					sy += y;
					++result.bright;
				}
				const double rx = x / scale, ry = y / scale;
				if (rx >= old_left + 4 && rx < old_left + Square - 4 && ry >= old_top + 4 &&
				    ry < old_top + Square - 4) {
					old_sum += (p[0] + p[1] + p[2]) / 3.0;
					++old_count;
				}
			}
		}
		if (result.bright) {
			result.center_x = sx / result.bright;
			result.center_y = sy / result.bright;
		}
		result.old_area_brightness = old_count ? old_sum / old_count : 0;
		// Mean absolute error over a background corner block.
		double error = 0;
		for (uint32_t y = 4; y < 36; ++y)
			for (uint32_t x = 4; x < 36; ++x)
				for (int c = 0; c < 3; ++c)
					error += std::abs(out[(size_t(y) * ow + x) * 4 + c] - Background[c]);
		result.corner_error = error / (32.0 * 32.0 * 3.0);
		return result;
	}

	// Records, submits and checks one real frame.
	Readback RunFrame(uint64_t id, uint32_t index, float left, float top, float step, bool cut,
	                  float old_left, float old_top) {
		const auto frame = Frame(id, index, cut);
		Upload(frame, left, top, step);
		gpu.Begin();
		RecordUploads();
		std::string error;
		Check(backend.Record(static_cast<VkCommandBuffer>(gpu.command), frame, error),
		      "DLSS record frame " + std::to_string(id) + ": " + error);
		RecordReadback();
		gpu.SubmitAndWait();
		return Analyze(old_left, old_top);
	}

	void Save(const std::filesystem::path& path) const {
		const auto    ow = scene.options.output_width, oh = scene.options.output_height;
		std::ofstream file(path, std::ios::binary);
		file << "P6\n" << ow << ' ' << oh << "\n255\n";
		const auto* out = gpu.mapped + OutputOffset();
		for (size_t p = 0; p < size_t(ow) * oh; ++p)
			file.write(reinterpret_cast<const char*>(out + p * 4), 3);
		Check(bool(file), "save " + path.string());
	}
};

void CheckFrame(const Probe& probe, const Readback& r, float left, float top, uint64_t id,
                bool warm) {
	const auto&  o     = probe.scene.options;
	const double scale = static_cast<double>(o.output_width) / o.render_width;
	const double ex = (left + Square / 2.0) * scale, ey = (top + Square / 2.0) * scale;
	const double area = Square * scale * Square * scale;
	std::printf("frame %llu: bright %zu (expected ~%.0f), center %.1f,%.1f (expected %.1f,%.1f), "
	            "background error %.1f, sentinel %zu\n",
	            static_cast<unsigned long long>(id), r.bright, area, r.center_x, r.center_y, ex, ey,
	            r.corner_error, r.sentinel);
	Check(r.sentinel == 0, "DLSS left output pixels unwritten");
	Check(r.corner_error < 12.0, "DLSS changed the static background");
	if (!warm) return;
	Check(r.bright > area * 0.6 && r.bright < area * 1.4,
	      "the moving square has the wrong upscaled size");
	Check(std::abs(r.center_x - ex) < 4.0 * scale && std::abs(r.center_y - ey) < 4.0 * scale,
	      "the moving square is not at its upscaled position");
}

} // namespace

int wmain(int argc, wchar_t** argv) {
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	std::setvbuf(stderr, nullptr, _IONBF, 0);
	if (argc == 3 && std::wstring(argv[1]) == L"--runtime-info") {
		const auto info = InspectRuntime(std::filesystem::absolute(argv[2]));
		return info.signed_valid ? Pass : Fail;
	}
	if (argc < 4 || argc > 5 || (argc == 5 && std::wstring(argv[4]) != L"--validation")) {
		std::fprintf(
		    stderr,
		    "Usage: dlss_vulkan_probe <feature dir with nvngx_dlss.dll> <data dir> <output.ppm> "
		    "[--validation]\n       dlss_vulkan_probe --runtime-info <nvngx_dlss.dll>\n");
		return Fail;
	}
	const bool validation = argc == 5;
	const auto feature    = std::filesystem::absolute(argv[1]);
	const auto data       = std::filesystem::absolute(argv[2]);
	const auto output     = std::filesystem::absolute(argv[3]);
	const auto runtime    = feature / L"nvngx_dlss.dll";
	std::puts("DLSS SR offscreen probe. Synthetic frames only; no game, presentation or FG.");
	if (!std::filesystem::is_regular_file(runtime))
		Exit(Skip, "NVIDIA's DLSS feature runtime is not at " + runtime.string() +
		               "; DLSS NOT RUN. Provide the official signed nvngx_dlss.dll.");
	Gpu gpu;
	if (!gpu.LoadLoader()) Exit(Skip, "no Vulkan loader; DLSS NOT RUN");
	std::string names;
	if (!Gpu::HasNvidiaGpu(names))
		Exit(Skip, "no NVIDIA Vulkan GPU (found: " + (names.empty() ? std::string("none") : names) +
		               "); DLSS NOT RUN and no NGX function was called");
	const auto info = InspectRuntime(runtime);
	Check(info.signed_valid, "the feature runtime's Authenticode signature is not valid");
	Check(info.signer.find("NVIDIA") != std::string::npos,
	      "the feature runtime is not signed by NVIDIA (signer \"" + info.signer + "\")");
	std::error_code created;
	std::filesystem::create_directories(data, created);
	DlssApplication     application {ProjectId, EngineVersion, data, feature};
	DlssNgxRequirements requirements;
	std::string         error;
	Check(
	    QueryDlssNgxRequirements(application, VK_NULL_HANDLE, VK_NULL_HANDLE, requirements, error),
	    "NGX instance requirements: " + error);
	gpu.CreateInstance(requirements.instance_extensions, validation);
	Check(QueryDlssNgxRequirements(application, static_cast<VkInstance>(gpu.instance),
	                               static_cast<VkPhysicalDevice>(gpu.physical), requirements,
	                               error),
	      "NGX device requirements: " + error);
	gpu.CreateDevice(requirements.device_extensions);
	const auto& p = gpu.properties;
	std::printf("GPU %s, driver %u.%u (raw 0x%08x), Vulkan %u.%u.%u; NGX instance extensions %zu, "
	            "device extensions %zu\n",
	            p.deviceName.data(), (p.driverVersion >> 22u) & 0x3ffu,
	            (p.driverVersion >> 14u) & 0xffu, p.driverVersion,
	            VK_API_VERSION_MAJOR(p.apiVersion), VK_API_VERSION_MINOR(p.apiVersion),
	            VK_API_VERSION_PATCH(p.apiVersion), requirements.instance_extensions.size(),
	            requirements.device_extensions.size());

	Probe probe(gpu, application);
	// Wrong identity and a second simultaneous adapter fail before any feature exists.
	{
		DlssBackend bad;
		auto        bad_app = application;
		bad_app.project_id  = "not-a-guid";
		Check(!bad.Initialize(bad_app, probe.device,
		                      {640, 360, 1280, 720, DlssQuality::Performance}, CreateDlssNgxSdk(),
		                      error),
		      "an invalid project ID must be rejected");
	}
	probe.Configure({640, 360, 1280, 720, DlssQuality::Performance});
	{
		DlssBackend second;
		Check(!second.Initialize(application, probe.device, probe.scene.options, CreateDlssNgxSdk(),
		                         error) &&
		          error.find("already active") != std::string::npos,
		      "a second simultaneous NGX adapter must be rejected");
	}
	// Consecutive motion, 4 render pixels per frame.
	uint64_t id   = 1;
	float    left = 100, top = 150;
	for (uint32_t n = 0; n < 8; ++n, ++id) {
		left         = 100.0f + 4.0f * n;
		const auto r = probe.RunFrame(id, n, left, top, n == 0 ? 0.0f : 4.0f, n == 0, left, top);
		CheckFrame(probe, r, left, top, id, n >= 3);
	}
	// Camera cut: the square jumps; with reset the old position must not ghost.
	const float old_left = left, old_top = top;
	left   = 400;
	top    = 60;
	auto r = probe.RunFrame(id, 8, left, top, 0.0f, true, old_left, old_top);
	CheckFrame(probe, r, left, top, id, true);
	std::printf("cut: old-area brightness %.1f\n", r.old_area_brightness);
	Check(r.old_area_brightness < 120.0, "the old square ghosts after a reset camera cut");
	// A duplicate real frame is rejected without SDK work.
	{
		const auto frame = probe.Frame(id, 9, false);
		gpu.Begin();
		Check(!probe.backend.Record(static_cast<VkCommandBuffer>(gpu.command), frame, error) &&
		          error.find("duplicate") != std::string::npos,
		      "a duplicate real frame must be rejected");
		Require(gpu.command.end(), "end unused command buffer");
	}
	// A gap in real frame IDs resets history and still records.
	id += 2;
	r = probe.RunFrame(id, 10, left, top, 0.0f, false, left, top);
	CheckFrame(probe, r, left, top, id, true);
	// A successful recording that is discarded requires reinitialization.
	++id;
	{
		const auto frame = probe.Frame(id, 11, false);
		probe.Upload(frame, left, top, 0.0f);
		gpu.Begin();
		probe.RecordUploads();
		Check(probe.backend.Record(static_cast<VkCommandBuffer>(gpu.command), frame, error),
		      "record a frame to discard: " + error);
		Require(gpu.command.end(), "end discarded command buffer");
		Require(gpu.command.reset({}), "discard the recording");
		probe.backend.Shutdown();
		probe.Initialize();
	}
	++id;
	r = probe.RunFrame(id, 12, left, top, 0.0f, true, left, top);
	CheckFrame(probe, r, left, top, id, true);
	// A second configuration: Quality, 960x540 to 1440x810.
	probe.Configure({960, 540, 1440, 810, DlssQuality::Quality});
	for (uint32_t n = 0; n < 6; ++n) {
		++id;
		left = 300.0f + 3.0f * n;
		top  = 200;
		r    = probe.RunFrame(id, 13 + n, left, top, n == 0 ? 0.0f : 3.0f, n == 0, left, top);
		CheckFrame(probe, r, left, top, id, n >= 3);
	}
	probe.Save(output);
	probe.backend.Shutdown();
	for (auto* image:
	     {&probe.scene.color, &probe.scene.depth, &probe.scene.motion, &probe.scene.output})
		gpu.Destroy(*image);
	if (validation)
		Check(ErrorMessages == 0, std::to_string(ErrorMessages) + " Vulkan validation error(s)");
	std::printf(
	    "REPORT "
	    "{\"gpu\":\"%s\",\"driver_raw\":%u,\"runtime_sha256\":\"%s\",\"runtime_version\":\"%s\","
	    "\"runtime_signer\":\"%s\",\"frames\":%llu,\"validation\":%s,\"validation_errors\":%u}\n",
	    p.deviceName.data(), p.driverVersion, info.sha256.c_str(), info.version.c_str(),
	    info.signer.c_str(), static_cast<unsigned long long>(id), validation ? "true" : "false",
	    ErrorMessages);
	Exit(Pass, "DLSS SR offscreen dispatches with readback, cut/reset, duplicate, gap, "
	           "discard/reinitialize "
	           "and reconfiguration checks. Synthetic frames only: no game input, presentation, "
	           "quality or "
	           "performance result.");
}
