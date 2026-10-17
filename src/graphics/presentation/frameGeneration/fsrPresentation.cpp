#include "graphics/presentation/frameGeneration/fsrPresentation.h"

#include "graphics/presentation/frameGeneration/fsrFrameValidation.h"

#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <ffx_api/ffx_api.h>
#include <ffx_api/ffx_api_loader.h>
#include <ffx_api/ffx_framegeneration.h>
#include <ffx_api/ffx_upscale.h>
#include <ffx_api/vk/ffx_api_vk.h>

namespace Libs::Graphics::FrameGeneration {
namespace {
FfxApiResource Resource(const FsrImage& image, bool output = false) {
	FfxApiResource resource {};
	resource.resource           = reinterpret_cast<void*>(image.image);
	resource.description.type   = FFX_API_RESOURCE_TYPE_TEXTURE2D;
	resource.description.format = ffxApiGetSurfaceFormatVK(image.format);
	resource.description.width  = image.width;
	resource.description.height = image.height;
	resource.description.depth = resource.description.mipCount = 1;
	resource.description.usage =
	    output ? FFX_API_RESOURCE_USAGE_UAV : FFX_API_RESOURCE_USAGE_READ_ONLY;
	resource.state =
	    output ? FFX_API_RESOURCE_STATE_UNORDERED_ACCESS : FFX_API_RESOURCE_STATE_COMPUTE_READ;
	return resource;
}
bool Check(ffxReturnCode_t result, const char* operation, std::string& error) {
	if (result == FFX_API_RETURN_OK) return true;
	error = std::string(operation) + " failed (" + std::to_string(result) + ")";
	return false;
}
bool ValidDevice(VkDevice device, VkPhysicalDevice physical, PFN_vkGetDeviceProcAddr get_proc,
                 uint32_t width, uint32_t height, VkFormat format, std::string& error) {
	if (!device || !physical || !get_proc || width == 0 || height == 0 || width > 16384 ||
	    height > 16384 ||
	    (format != VK_FORMAT_R8G8B8A8_UNORM && format != VK_FORMAT_B8G8R8A8_UNORM) ||
	    !get_proc(device, "vkGetBufferMemoryRequirements2KHR") ||
	    !get_proc(device, "vkDeviceWaitIdle")) {
		error = "FSR requires an SDR swapchain and the enabled Vulkan SDK features";
		return false;
	}
	return true;
}
} // namespace

struct FsrPresentation::Impl {
	HMODULE      library = nullptr;
	ffxFunctions api {};
	ffxContext   generation = nullptr, upscale = nullptr, swapchain = nullptr;
	ffxQueryDescSwapchainReplacementFunctionsVK functions {};
	VkDevice                                    device    = VK_NULL_HANDLE;
	VkPhysicalDevice                            physical  = VK_NULL_HANDLE;
	VkSwapchainKHR                              handle    = VK_NULL_HANDLE;
	PFN_vkDeviceWaitIdle                        wait_idle = nullptr;
	uint32_t                                    width = 0, height = 0;
	uint64_t                                    sequence = 0;
	TemporalHistory                             history;
	std::array<std::atomic<uint64_t>, 8>        reset_ids {};

	bool Configure(bool enabled, bool reset, std::string& error) {
		if (!generation || !swapchain) return false;
		ffxConfigureDescFrameGeneration config {};
		config.header.type             = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
		config.frameGenerationEnabled  = enabled;
		config.frameGenerationCallback = [](ffxDispatchDescFrameGeneration* description,
		                                    void*                           data) {
			auto& self = *static_cast<Impl*>(data);
			description->reset =
			    description->reset ||
			    self.reset_ids[description->frameID % self.reset_ids.size()].load() ==
			        description->frameID + 1;
			return self.api.Dispatch(&self.generation, &description->header);
		};
		config.frameGenerationCallbackUserContext = this;
		config.swapChain                          = reinterpret_cast<void*>(handle);
		config.generationRect = {0, 0, static_cast<int32_t>(width), static_cast<int32_t>(height)};
		config.frameID        = sequence;
		reset_ids[sequence % reset_ids.size()].store(reset ? sequence + 1 : 0);
		return Check(api.Configure(&generation, &config.header), "FSR configure generation", error);
	}
};

FsrPresentation::FsrPresentation(): m(std::make_unique<Impl>()) {}
FsrPresentation::~FsrPresentation() {
	Shutdown();
}

bool FsrPresentation::Initialize(VkDevice device, VkPhysicalDevice physical,
                                 PFN_vkGetDeviceProcAddr get_proc, uint32_t width, uint32_t height,
                                 VkFormat format, bool upscale, bool generate, std::string& error) {
	Shutdown();
	if (!ValidDevice(device, physical, get_proc, width, height, format, error)) return false;
	std::array<wchar_t, 32768> executable {};
	const auto                 length =
	    GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
	if (length == 0 || length >= executable.size()) {
		error = "Cannot locate emulator directory";
		return false;
	}
	const auto library = std::filesystem::path(executable.data()).parent_path() /
	                     L"Experiments/FSR3/amd_fidelityfx_vk.dll";
	m->library =
	    LoadLibraryExW(library.c_str(), nullptr,
		               LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
	if (!m->library) {
		error = "The bundled FidelityFX Vulkan library is missing or cannot load";
		return false;
	}
	ffxFunctions functions {};
	ffxLoadFunctions(&functions, m->library);
	return CreateContexts(functions, device, physical, get_proc, width, height, format, upscale,
	                      generate, error);
}

bool FsrPresentation::InitializeWithFunctions(const ffxFunctions& functions, VkDevice device,
                                              VkPhysicalDevice physical,
                                              PFN_vkGetDeviceProcAddr get_proc, uint32_t width,
                                              uint32_t height, VkFormat format, bool upscale,
                                              bool generate, std::string& error) {
	Shutdown();
	if (!ValidDevice(device, physical, get_proc, width, height, format, error)) return false;
	return CreateContexts(functions, device, physical, get_proc, width, height, format, upscale,
	                      generate, error);
}

bool FsrPresentation::CreateContexts(const ffxFunctions& functions, VkDevice device,
                                     VkPhysicalDevice physical, PFN_vkGetDeviceProcAddr get_proc,
                                     uint32_t width, uint32_t height, VkFormat format,
                                     bool upscale, bool generate, std::string& error) {
	m->api = functions;
	if (!m->api.CreateContext || !m->api.DestroyContext || !m->api.Configure || !m->api.Dispatch ||
	    !m->api.Query) {
		error = "Incomplete FidelityFX Vulkan API";
		Shutdown();
		return false;
	}
	m->device    = device;
	m->physical  = physical;
	m->wait_idle = reinterpret_cast<PFN_vkDeviceWaitIdle>(get_proc(device, "vkDeviceWaitIdle"));
	m->width     = width;
	m->height    = height;
	ffxCreateBackendVKDesc backend {};
	backend.header.type      = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
	backend.vkDevice         = device;
	backend.vkPhysicalDevice = physical;
	backend.vkDeviceProcAddr = get_proc;
	if (upscale) {
		ffxCreateContextDescUpscale create {};
		create.header.type  = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
		create.header.pNext = &backend.header;
		create.flags = FFX_UPSCALE_ENABLE_AUTO_EXPOSURE | FFX_UPSCALE_ENABLE_DYNAMIC_RESOLUTION |
		               FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE;
		create.maxRenderSize = create.maxUpscaleSize = {width, height};
		if (!Check(m->api.CreateContext(&m->upscale, &create.header, nullptr),
		           "FSR create upscaler", error)) {
			Shutdown();
			return false;
		}
	}
	if (!generate) return true;
	ffxCreateContextDescFrameGeneration create {};
	create.header.type  = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
	create.header.pNext = &backend.header;
	create.displaySize = create.maxRenderSize = {width, height};
	create.backBufferFormat                   = ffxApiGetSurfaceFormatVK(format);
	if (!Check(m->api.CreateContext(&m->generation, &create.header, nullptr),
	           "FSR create generation", error)) {
		Shutdown();
		return false;
	}
	return true;
}

bool FsrPresentation::CreateSwapchain(const VkSwapchainCreateInfoKHR& info, VkQueue game,
                                      VkQueue present, VkQueue acquire, VkQueue compute,
                                      uint32_t family, QueueSubmitCallback submit,
                                      VkSwapchainKHR& swapchain, std::string& error) {
	if (!m->api.CreateContext || !game || !present || !acquire || !compute) return false;
	ffxCreateContextDescFrameGenerationSwapChainVK create {};
	create.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FGSWAPCHAIN_VK;
	ffxCreateContextDescFrameGenerationSwapChainModeVK mode {};
	mode.header.type           = FFX_API_CREATE_CONTEXT_DESC_TYPE_FGSWAPCHAIN_MODE_VK;
	mode.composeOnPresentQueue = true;
	create.header.pNext        = &mode.header;
	create.physicalDevice      = m->physical;
	create.device              = m->device;
	create.createInfo          = info;
	create.swapchain           = &m->handle;
	create.gameQueue           = {game, family, submit};
	create.presentQueue        = {present, family, nullptr};
	create.imageAcquireQueue   = {acquire, family, nullptr};
	create.asyncComputeQueue   = {compute, family, nullptr};
	if (!Check(m->api.CreateContext(&m->swapchain, &create.header, nullptr), "FSR create swapchain",
	           error))
		return false;
	m->functions.header.type = FFX_API_QUERY_DESC_TYPE_FGSWAPCHAIN_FUNCTIONS_VK;
	if (!Check(m->api.Query(&m->swapchain, &m->functions.header), "FSR query swapchain", error) ||
	    !m->functions.pOutAcquireNextImageKHR || !m->functions.pOutGetSwapchainImagesKHR ||
	    !m->functions.pOutQueuePresentKHR || m->handle == VK_NULL_HANDLE) {
		m->api.DestroyContext(&m->swapchain, nullptr);
		m->swapchain = nullptr;
		m->handle    = VK_NULL_HANDLE;
		return false;
	}
	swapchain = m->handle;
	return true;
}

bool FsrPresentation::OwnsSwapchain() const {
	return m->swapchain != nullptr;
}
VkResult FsrPresentation::GetImages(VkSwapchainKHR swapchain, uint32_t* count, VkImage* images) {
	return m->functions.pOutGetSwapchainImagesKHR(m->device, swapchain, count, images);
}
VkResult FsrPresentation::Acquire(VkSwapchainKHR swapchain, VkSemaphore semaphore,
                                  uint32_t* index) {
	return m->functions.pOutAcquireNextImageKHR(m->device, swapchain, UINT64_MAX, semaphore,
	                                            VK_NULL_HANDLE, index);
}
VkResult FsrPresentation::Present(VkQueue queue, const VkPresentInfoKHR& info) {
	return m->functions.pOutQueuePresentKHR(queue, &info);
}
uint64_t FsrPresentation::PresentCount(VkSwapchainKHR swapchain) const {
	return m->functions.pOutGetLastPresentCountFFXAPI
	           ? m->functions.pOutGetLastPresentCountFFXAPI(swapchain)
			   : 0;
}

bool FsrPresentation::Record(VkCommandBuffer command, const FsrFrame& frame, bool generate,
                             std::string& error) {
	error.clear();
	if ((!m->generation && !m->upscale) || !command) {
		error = "FSR context or command buffer is unavailable";
		Suspend();
		return false;
	}
	if (const auto* invalid =
	        ValidateFsrPresentationFrame(frame, m->width, m->height, m->upscale != nullptr)) {
		error = invalid;
		Suspend();
		return false;
	}
	if (m->history.IsDuplicate(frame.id)) {
		error = "FSR duplicate guest frame cannot advance temporal history";
		Suspend();
		return false;
	}
	const bool reset = m->history.NeedsReset(frame.id, frame.depth.width, frame.depth.height,
	                                         frame.reset, generate && m->swapchain);
	++m->sequence;
	if (m->swapchain && !m->Configure(generate, reset, error)) {
		Suspend();
		return false;
	}
	bool upscaled = false;
	if (m->upscale) {
		ffxDispatchDescUpscale dispatch {};
		dispatch.header.type       = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
		dispatch.commandList       = command;
		dispatch.color             = Resource(frame.color);
		dispatch.depth             = Resource(frame.depth);
		dispatch.motionVectors     = Resource(frame.motion);
		dispatch.output            = Resource(frame.output, true);
		dispatch.renderSize        = {frame.depth.width, frame.depth.height};
		dispatch.upscaleSize       = {m->width, m->height};
		dispatch.motionVectorScale = {static_cast<float>(frame.depth.width),
		                              static_cast<float>(frame.depth.height)};
		// This compatibility adapter retains the game's TAA/postprocessing. Its
		// final color is already resolved; do not invent a new camera jitter.
		dispatch.jitterOffset            = {0, 0};
		dispatch.frameTimeDelta          = frame.milliseconds;
		dispatch.preExposure             = 1;
		dispatch.reset                   = reset;
		dispatch.cameraNear              = frame.near_plane;
		dispatch.cameraFar               = frame.far_plane;
		dispatch.cameraFovAngleVertical  = frame.vertical_fov;
		dispatch.viewSpaceToMetersFactor = frame.view_space_to_meters;
		dispatch.flags                   = FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_SRGB;
		if (!Check(m->api.Dispatch(&m->upscale, &dispatch.header), "FSR upscale", error)) {
			Suspend();
			return false;
		}
		upscaled = true;
	}
	if (generate && m->swapchain) {
		ffxDispatchDescFrameGenerationPrepareCameraInfo camera {};
		camera.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_CAMERAINFO;
		std::copy_n(frame.camera_position.data(), 3, camera.cameraPosition);
		std::copy_n(frame.camera_right.data(), 3, camera.cameraRight);
		std::copy_n(frame.camera_up.data(), 3, camera.cameraUp);
		std::copy_n(frame.camera_forward.data(), 3, camera.cameraForward);
		ffxDispatchDescFrameGenerationPrepare prepare {};
		prepare.header.type             = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE;
		prepare.header.pNext            = &camera.header;
		prepare.commandList             = command;
		prepare.frameID                 = m->sequence;
		prepare.renderSize              = {frame.depth.width, frame.depth.height};
		prepare.depth                   = Resource(frame.depth);
		prepare.motionVectors           = Resource(frame.motion);
		prepare.motionVectorScale       = {static_cast<float>(frame.depth.width),
		                                   static_cast<float>(frame.depth.height)};
		prepare.frameTimeDelta          = frame.milliseconds;
		prepare.cameraNear              = frame.near_plane;
		prepare.cameraFar               = frame.far_plane;
		prepare.cameraFovAngleVertical  = frame.vertical_fov;
		prepare.viewSpaceToMetersFactor = frame.view_space_to_meters;
		if (!Check(m->api.Dispatch(&m->generation, &prepare.header), "FSR prepare generation",
		           error)) {
			Suspend();
			return false;
		}
	}
	m->history.Commit(frame.id, frame.depth.width, frame.depth.height, generate && m->swapchain);
	return upscaled;
}

void FsrPresentation::Suspend() {
	std::string ignored;
	if (m->generation && m->swapchain) m->Configure(false, true, ignored);
	m->history.Invalidate();
}
void FsrPresentation::WaitForPresents() {
	if (!m->swapchain) return;
	ffxDispatchDescFrameGenerationSwapChainWaitForPresentsVK wait {};
	wait.header.type = FFX_API_DISPATCH_DESC_TYPE_FGSWAPCHAIN_WAIT_FOR_PRESENTS_VK;
	m->api.Dispatch(&m->swapchain, &wait.header);
}
void FsrPresentation::Shutdown() {
	Suspend();
	WaitForPresents();
	if (m->device && m->wait_idle) m->wait_idle(m->device);
	if (m->generation) m->api.DestroyContext(&m->generation, nullptr);
	if (m->upscale) m->api.DestroyContext(&m->upscale, nullptr);
	if (m->swapchain) m->api.DestroyContext(&m->swapchain, nullptr);
	if (m->library) FreeLibrary(m->library);
	m = std::make_unique<Impl>();
}

} // namespace Libs::Graphics::FrameGeneration
