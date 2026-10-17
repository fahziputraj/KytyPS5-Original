#include "graphics/presentation/frameGeneration/fsr3Backend.h"

#include "graphics/presentation/frameGeneration/fsrFrameValidation.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <ffx_api/ffx_api.h>
#include <ffx_api/ffx_api_loader.h>
#include <ffx_api/ffx_framegeneration.h>
#include <ffx_api/vk/ffx_api_vk.h>

namespace Libs::Graphics::FrameGeneration {

struct Fsr3Backend::Impl {
	HMODULE              library = nullptr;
	ffxFunctions         api {};
	ffxContext           context   = nullptr;
	VkDevice             device    = VK_NULL_HANDLE;
	PFN_vkDeviceWaitIdle wait_idle = nullptr;
	uint32_t             width = 0, height = 0;
	VkFormat             format = VK_FORMAT_UNDEFINED;
	TemporalHistory      history;
};

namespace {
bool Failure(std::string& error, const char* operation, uint32_t code) {
	error = std::string(operation) + " failed (" + std::to_string(code) + ")";
	return false;
}

FfxApiResource Resource(const FsrImage& image, bool output = false) {
	FfxApiResource result {};
	result.resource             = reinterpret_cast<void*>(image.image);
	result.description.type     = FFX_API_RESOURCE_TYPE_TEXTURE2D;
	result.description.format   = ffxApiGetSurfaceFormatVK(image.format);
	result.description.width    = image.width;
	result.description.height   = image.height;
	result.description.depth    = 1;
	result.description.mipCount = 1;
	result.description.usage    = output ? FFX_API_RESOURCE_USAGE_UAV
	                              : image.format == VK_FORMAT_D32_SFLOAT
	                                  ? FFX_API_RESOURCE_USAGE_DEPTHTARGET
	                                  : FFX_API_RESOURCE_USAGE_READ_ONLY;
	result.state =
	    output ? FFX_API_RESOURCE_STATE_UNORDERED_ACCESS : FFX_API_RESOURCE_STATE_COMPUTE_READ;
	return result;
}
} // namespace

Fsr3Backend::Fsr3Backend(): m_impl(std::make_unique<Impl>()) {}
Fsr3Backend::~Fsr3Backend() {
	Shutdown();
}

void Fsr3Backend::Shutdown() {
	if (m_impl->context != nullptr) {
		m_impl->wait_idle(m_impl->device);
		m_impl->api.DestroyContext(&m_impl->context, nullptr);
	}
	if (m_impl->library != nullptr) FreeLibrary(m_impl->library);
	*m_impl = {};
}

bool Fsr3Backend::Initialize(const std::filesystem::path& library, VkDevice device,
                             VkPhysicalDevice physical, PFN_vkGetDeviceProcAddr get_proc,
                             uint32_t width, uint32_t height, VkFormat format, std::string& error) {
	Shutdown();
	if (!library.is_absolute() || device == VK_NULL_HANDLE || physical == VK_NULL_HANDLE ||
	    get_proc == nullptr || width == 0 || height == 0 || width > 16384 || height > 16384 ||
	    format != VK_FORMAT_R8G8B8A8_UNORM) {
		error = "FSR3 requires an explicit SDK library, valid Vulkan device and display size";
		return false;
	}
	// SDK 1.1.4 queries dedicated-allocation support from the physical device
	// and then calls this KHR alias, even on a core Vulkan 1.3 device. Refuse a
	// logical device without the entry point instead of entering a null call.
	if (!get_proc(device, "vkGetBufferMemoryRequirements2KHR")) {
		error = "FSR3 SDK requires VK_KHR_get_memory_requirements2 enabled on the logical device";
		return false;
	}
	m_impl->library =
	    LoadLibraryExW(library.c_str(), nullptr,
		               LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
	if (m_impl->library == nullptr) return Failure(error, "LoadLibraryExW", GetLastError());
	ffxLoadFunctions(&m_impl->api, m_impl->library);
	if (!m_impl->api.CreateContext || !m_impl->api.DestroyContext || !m_impl->api.Configure ||
	    !m_impl->api.Query || !m_impl->api.Dispatch) {
		error = "SDK library does not provide the complete FidelityFX API";
		Shutdown();
		return false;
	}
	m_impl->device = device;
	m_impl->wait_idle =
	    reinterpret_cast<PFN_vkDeviceWaitIdle>(get_proc(device, "vkDeviceWaitIdle"));
	if (!m_impl->wait_idle) {
		error = "Vulkan device has no vkDeviceWaitIdle";
		Shutdown();
		return false;
	}
	ffxCreateBackendVKDesc backend {};
	backend.header.type      = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
	backend.vkDevice         = device;
	backend.vkPhysicalDevice = physical;
	backend.vkDeviceProcAddr = get_proc;
	ffxCreateContextDescFrameGeneration create {};
	create.header.type  = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
	create.header.pNext = &backend.header;
	create.flags        = FFX_FRAMEGENERATION_ENABLE_DEBUG_CHECKING;
	create.displaySize = create.maxRenderSize = {width, height};
	create.backBufferFormat                   = ffxApiGetSurfaceFormatVK(format);
	const auto status = m_impl->api.CreateContext(&m_impl->context, &create.header, nullptr);
	if (status != FFX_API_RETURN_OK) {
		Failure(error, "FSR3 context creation", status);
		Shutdown();
		return false;
	}
	m_impl->width  = width;
	m_impl->height = height;
	m_impl->format = format;
	return true;
}

void Fsr3Backend::ResetHistory() {
	m_impl->history.Invalidate();
}

bool Fsr3Backend::Record(VkCommandBuffer command, const FsrFrame& frame, std::string& error) {
	error.clear();
	if (!m_impl->context || command == VK_NULL_HANDLE) {
		error = "FSR3 context or command buffer is unavailable";
		ResetHistory();
		return false;
	}
	if (const auto* invalid =
	        ValidateFsrProbeFrame(frame, m_impl->width, m_impl->height, m_impl->format)) {
		error = invalid;
		ResetHistory();
		return false;
	}
	if (m_impl->history.IsDuplicate(frame.id)) {
		error = "FSR3 duplicate guest frame cannot advance temporal history";
		ResetHistory();
		return false;
	}
	const bool reset = m_impl->history.NeedsReset(frame.id, frame.depth.width, frame.depth.height,
	                                              frame.reset, true);
	ffxConfigureDescFrameGeneration configure {};
	configure.header.type            = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
	configure.frameGenerationEnabled = true;
	configure.flags                  = FFX_FRAMEGENERATION_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY;
	configure.generationRect         = {0, 0, static_cast<int32_t>(m_impl->width),
	                                    static_cast<int32_t>(m_impl->height)};
	configure.frameID                = frame.id;
	auto status                      = m_impl->api.Configure(&m_impl->context, &configure.header);
	if (status != FFX_API_RETURN_OK) {
		ResetHistory();
		return Failure(error, "FSR3 configure", status);
	}
	ffxDispatchDescFrameGenerationPrepareCameraInfo camera {};
	camera.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_CAMERAINFO;
	std::copy_n(frame.camera_position.data(), 3, camera.cameraPosition);
	std::copy_n(frame.camera_right.data(), 3, camera.cameraRight);
	std::copy_n(frame.camera_up.data(), 3, camera.cameraUp);
	std::copy_n(frame.camera_forward.data(), 3, camera.cameraForward);
	ffxDispatchDescFrameGenerationPrepare prepare {};
	prepare.header.type             = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE;
	prepare.header.pNext            = &camera.header;
	prepare.frameID                 = frame.id;
	prepare.commandList             = command;
	prepare.renderSize              = {m_impl->width, m_impl->height};
	prepare.jitterOffset            = {frame.jitter_x, frame.jitter_y};
	prepare.motionVectorScale       = {frame.motion_scale_x, frame.motion_scale_y};
	prepare.frameTimeDelta          = frame.milliseconds;
	prepare.cameraNear              = frame.near_plane;
	prepare.cameraFar               = frame.far_plane;
	prepare.cameraFovAngleVertical  = frame.vertical_fov;
	prepare.viewSpaceToMetersFactor = frame.view_space_to_meters;
	prepare.depth                   = Resource(frame.depth);
	prepare.motionVectors           = Resource(frame.motion);
	status                          = m_impl->api.Dispatch(&m_impl->context, &prepare.header);
	if (status != FFX_API_RETURN_OK) {
		ResetHistory();
		return Failure(error, "FSR3 prepare", status);
	}
	ffxDispatchDescFrameGeneration generate {};
	generate.header.type                = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION;
	generate.commandList                = command;
	generate.presentColor               = Resource(frame.color);
	generate.outputs[0]                 = Resource(frame.output, true);
	generate.numGeneratedFrames         = 1;
	generate.reset                      = reset;
	generate.frameID                    = frame.id;
	generate.backbufferTransferFunction = FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB;
	generate.minMaxLuminance[1]         = 1.0f;
	generate.generationRect             = configure.generationRect;
	status                              = m_impl->api.Dispatch(&m_impl->context, &generate.header);
	if (status != FFX_API_RETURN_OK) {
		ResetHistory();
		return Failure(error, "FSR3 generation", status);
	}
	m_impl->history.Commit(frame.id, frame.depth.width, frame.depth.height, true);
	return true;
}

} // namespace Libs::Graphics::FrameGeneration
