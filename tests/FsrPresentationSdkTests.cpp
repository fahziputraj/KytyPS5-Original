// FsrPresentation against a stub FidelityFX function table (review R1, R3). No SDK library,
// Vulkan driver or GPU is used: every handle is fake and only reaches the stubs below.
// 1. Recording order and failure handling: an upscale dispatch recorded before a failed
//    frame-generation prepare stays in the caller's command buffer (the presenter always
//    submits it), the frame is reported as failed, generation is configured off and the next
//    real frame resets both temporal histories.
// 2. The eight-entry reset-ID ring read by the SDK's frame-generation callback: a reset is seen
//    by a callback for its own frame, never by another frame, and survives up to seven newer
//    configured frames. This documents the bound; the SDK's actual in-flight depth still needs
//    RTX traces.

#include "graphics/presentation/frameGeneration/fsrPresentation.h"

#include <cstdio>
#include <ffx_api/ffx_api.h>
#include <ffx_api/ffx_api_loader.h>
#include <ffx_api/ffx_framegeneration.h>
#include <ffx_api/ffx_upscale.h>
#include <ffx_api/vk/ffx_api_vk.h>
#include <string>
#include <vector>

using namespace Libs::Graphics::FrameGeneration;

namespace {

struct Event {
	std::string kind;
	uint64_t    frame   = 0;
	bool        flag    = false; // reset (dispatches) or enabled (configure)
	void*       command = nullptr;
};

struct Stub {
	int                               upscale_tag = 0, generation_tag = 0, swapchain_tag = 0;
	std::vector<Event>                events;
	ffxReturnCode_t                   upscale_result      = FFX_API_RETURN_OK;
	ffxReturnCode_t                   prepare_result      = FFX_API_RETURN_OK;
	FfxApiFrameGenerationDispatchFunc callback            = nullptr;
	void*                             callback_data       = nullptr;
	int                               live_contexts       = 0;
	bool                              last_callback_reset = false;
} g_stub;

ffxReturnCode_t StubCreate(ffxContext* context, ffxCreateContextDescHeader* desc,
                           const ffxAllocationCallbacks*) {
	switch (desc->type) {
		case FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE: *context = &g_stub.upscale_tag; break;
		case FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION:
			*context = &g_stub.generation_tag;
			break;
		case FFX_API_CREATE_CONTEXT_DESC_TYPE_FGSWAPCHAIN_VK: {
			*context     = &g_stub.swapchain_tag;
			auto* create = reinterpret_cast<ffxCreateContextDescFrameGenerationSwapChainVK*>(desc);
			*create->swapchain = reinterpret_cast<VkSwapchainKHR>(uintptr_t(0x5C));
			break;
		}
		default: return FFX_API_RETURN_ERROR_UNKNOWN_DESCTYPE;
	}
	++g_stub.live_contexts;
	return FFX_API_RETURN_OK;
}

ffxReturnCode_t StubDestroy(ffxContext* context, const ffxAllocationCallbacks*) {
	if (*context != nullptr) --g_stub.live_contexts;
	*context = nullptr;
	return FFX_API_RETURN_OK;
}

ffxReturnCode_t StubConfigure(ffxContext* context, const ffxConfigureDescHeader* desc) {
	if (*context != &g_stub.generation_tag ||
	    desc->type != FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION)
		return FFX_API_RETURN_ERROR_PARAMETER;
	const auto* config   = reinterpret_cast<const ffxConfigureDescFrameGeneration*>(desc);
	g_stub.callback      = config->frameGenerationCallback;
	g_stub.callback_data = config->frameGenerationCallbackUserContext;
	g_stub.events.push_back({"configure", config->frameID, config->frameGenerationEnabled});
	return FFX_API_RETURN_OK;
}

VkResult VKAPI_CALL StubAcquire(VkDevice, VkSwapchainKHR, uint64_t, VkSemaphore, VkFence,
                                uint32_t* index) {
	*index = 0;
	return VK_SUCCESS;
}
VkResult VKAPI_CALL StubImages(VkDevice, VkSwapchainKHR, uint32_t* count, VkImage*) {
	*count = 0;
	return VK_SUCCESS;
}
VkResult VKAPI_CALL StubPresent(VkQueue, const VkPresentInfoKHR*) {
	return VK_SUCCESS;
}

ffxReturnCode_t StubQuery(ffxContext* context, ffxQueryDescHeader* desc) {
	if (*context != &g_stub.swapchain_tag ||
	    desc->type != FFX_API_QUERY_DESC_TYPE_FGSWAPCHAIN_FUNCTIONS_VK)
		return FFX_API_RETURN_ERROR_PARAMETER;
	auto* functions = reinterpret_cast<ffxQueryDescSwapchainReplacementFunctionsVK*>(desc);
	functions->pOutAcquireNextImageKHR   = StubAcquire;
	functions->pOutGetSwapchainImagesKHR = StubImages;
	functions->pOutQueuePresentKHR       = StubPresent;
	return FFX_API_RETURN_OK;
}

ffxReturnCode_t StubDispatch(ffxContext* context, const ffxDispatchDescHeader* desc) {
	switch (desc->type) {
		case FFX_API_DISPATCH_DESC_TYPE_UPSCALE: {
			if (*context != &g_stub.upscale_tag) return FFX_API_RETURN_ERROR_PARAMETER;
			const auto* dispatch = reinterpret_cast<const ffxDispatchDescUpscale*>(desc);
			g_stub.events.push_back({"upscale", 0, dispatch->reset, dispatch->commandList});
			return g_stub.upscale_result;
		}
		case FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE: {
			if (*context != &g_stub.generation_tag) return FFX_API_RETURN_ERROR_PARAMETER;
			const auto* prepare =
			    reinterpret_cast<const ffxDispatchDescFrameGenerationPrepare*>(desc);
			g_stub.events.push_back({"prepare", prepare->frameID, false, prepare->commandList});
			return g_stub.prepare_result;
		}
		case FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION: {
			const auto* generate = reinterpret_cast<const ffxDispatchDescFrameGeneration*>(desc);
			g_stub.last_callback_reset = generate->reset;
			return FFX_API_RETURN_OK;
		}
		case FFX_API_DISPATCH_DESC_TYPE_FGSWAPCHAIN_WAIT_FOR_PRESENTS_VK: return FFX_API_RETURN_OK;
		default: return FFX_API_RETURN_ERROR_UNKNOWN_DESCTYPE;
	}
}

VKAPI_ATTR VkResult VKAPI_CALL StubWaitIdle(VkDevice) {
	return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL StubMemoryRequirements(VkDevice, const VkBufferMemoryRequirementsInfo2*,
                                                  VkMemoryRequirements2*) {}
PFN_vkVoidFunction VKAPI_CALL StubDeviceProc(VkDevice, const char* name) {
	const std::string text = name;
	if (text == "vkDeviceWaitIdle") return reinterpret_cast<PFN_vkVoidFunction>(StubWaitIdle);
	if (text == "vkGetBufferMemoryRequirements2KHR")
		return reinterpret_cast<PFN_vkVoidFunction>(StubMemoryRequirements);
	return nullptr;
}

template <typename T>
T Fake(uintptr_t value) {
	return reinterpret_cast<T>(value);
}

FsrFrame Frame(uint64_t id) {
	FsrFrame frame;
	frame.color                = {Fake<VkImage>(0x10), VK_FORMAT_R8G8B8A8_UNORM, 960, 540};
	frame.depth                = {Fake<VkImage>(0x11), VK_FORMAT_R32_SFLOAT, 960, 540};
	frame.motion               = {Fake<VkImage>(0x12), VK_FORMAT_R16G16_SFLOAT, 960, 540};
	frame.output               = {Fake<VkImage>(0x13), VK_FORMAT_R8G8B8A8_UNORM, 1920, 1080};
	frame.id                   = id;
	frame.milliseconds         = 33.3f;
	frame.near_plane           = 0.1f;
	frame.far_plane            = 1000.0f;
	frame.vertical_fov         = 1.0f;
	frame.view_space_to_meters = 1.0f;
	frame.camera_right         = {1, 0, 0};
	frame.camera_up            = {0, 1, 0};
	frame.camera_forward       = {0, 0, 1};
	return frame;
}

const Event* Find(const std::vector<Event>& events, const char* kind, size_t from = 0) {
	for (size_t i = from; i < events.size(); ++i) {
		if (events[i].kind == kind) return &events[i];
	}
	return nullptr;
}

// Runs the SDK's frame-generation callback as its present thread would, `frame` being the
// frame it generates for, and returns the reset flag the SDK dispatch received.
bool Callback(uint64_t frame, bool sdk_reset = false) {
	ffxDispatchDescFrameGeneration dispatch {};
	dispatch.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION;
	dispatch.frameID     = frame;
	dispatch.reset       = sdk_reset;
	g_stub.callback(&dispatch, g_stub.callback_data);
	return g_stub.last_callback_reset;
}

} // namespace

int main() {
	int        failures = 0;
	int        checks   = 0;
	const auto check    = [&](bool ok, const char* name) {
		++checks;
		if (!ok) {
			std::fprintf(stderr, "FAIL: %s\n", name);
			++failures;
		}
	};
	ffxFunctions functions {StubCreate, StubDestroy, StubConfigure, StubQuery, StubDispatch};
	const auto   device   = Fake<VkDevice>(1);
	const auto   physical = Fake<VkPhysicalDevice>(2);
	const auto   command  = Fake<VkCommandBuffer>(3);
	std::string  error;
	{
		FsrPresentation incomplete;
		ffxFunctions    missing = functions;
		missing.Dispatch        = nullptr;
		check(!incomplete.InitializeWithFunctions(missing, device, physical, StubDeviceProc, 1920,
		                                          1080, VK_FORMAT_R8G8B8A8_UNORM, true, true,
		                                          error) &&
		          error == "Incomplete FidelityFX Vulkan API" && g_stub.live_contexts == 0,
		      "an incomplete function table creates no context");
	}
	FsrPresentation fsr;
	check(fsr.InitializeWithFunctions(functions, device, physical, StubDeviceProc, 1920, 1080,
	                                  VK_FORMAT_R8G8B8A8_UNORM, true, true, error) &&
	          g_stub.live_contexts == 2,
	      "upscale and generation contexts are created");
	VkSwapchainCreateInfoKHR info {};
	info.sType          = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	VkSwapchainKHR swap = VK_NULL_HANDLE;
	check(fsr.CreateSwapchain(info, Fake<VkQueue>(4), Fake<VkQueue>(5), Fake<VkQueue>(6),
	                          Fake<VkQueue>(7), 0, nullptr, swap, error) &&
	          swap != VK_NULL_HANDLE && fsr.OwnsSwapchain() && g_stub.live_contexts == 3,
	      "the replacement swapchain is created through the function table");

	// Frame 1 starts temporal history: both SDK paths are told to reset.
	g_stub.events.clear();
	check(fsr.Record(command, Frame(1), true, error) && error.empty(), "first frame records");
	auto* configure = Find(g_stub.events, "configure");
	auto* upscale   = Find(g_stub.events, "upscale");
	auto* prepare   = Find(g_stub.events, "prepare");
	check(configure && configure->flag && configure->frame == 1 && upscale && upscale->flag &&
	          upscale->command == command && prepare && prepare->frame == 1 &&
	          prepare->command == command,
	      "the first frame configures generation, then records a reset upscale and a prepare");
	check(Callback(1), "the first frame's generation callback sees its reset");
	check(!Callback(1 + 1), "a callback for another frame does not see that reset");

	// Frame 2 continues history.
	g_stub.events.clear();
	check(fsr.Record(command, Frame(2), true, error), "consecutive frame records");
	upscale = Find(g_stub.events, "upscale");
	check(upscale && !upscale->flag && !Callback(2), "a consecutive frame does not reset history");

	// R1: the upscale dispatch succeeds and is recorded, then the generation prepare fails.
	g_stub.events.clear();
	g_stub.prepare_result = FFX_API_RETURN_ERROR_RUNTIME_ERROR;
	check(!fsr.Record(command, Frame(3), true, error) &&
	          error.find("FSR prepare generation failed") != std::string::npos,
	      "a failed prepare fails the frame with a diagnostic");
	g_stub.prepare_result = FFX_API_RETURN_OK;
	upscale               = Find(g_stub.events, "upscale");
	check(upscale && upscale->command == command,
	      "the already recorded upscale dispatch stays in the caller's command buffer");
	check(g_stub.events.size() >= 4 && g_stub.events.back().kind == "configure" &&
	          !g_stub.events.back().flag && g_stub.events.back().frame == 3,
	      "the failure configures frame generation off for that frame");
	check(Callback(3), "a late callback for the failed frame is told to reset");
	g_stub.events.clear();
	check(fsr.Record(command, Frame(4), true, error), "the next real frame records");
	configure = Find(g_stub.events, "configure");
	upscale   = Find(g_stub.events, "upscale");
	check(configure && configure->flag && configure->frame == 4 && upscale && upscale->flag &&
	          Callback(4),
	      "after a failed frame both upscale and generation histories reset");

	// An upscale failure records no prepare and suspends generation.
	g_stub.events.clear();
	g_stub.upscale_result = FFX_API_RETURN_ERROR_RUNTIME_ERROR;
	check(!fsr.Record(command, Frame(5), true, error) &&
	          error.find("FSR upscale failed") != std::string::npos,
	      "a failed upscale fails the frame");
	g_stub.upscale_result = FFX_API_RETURN_OK;
	check(!Find(g_stub.events, "prepare") && g_stub.events.back().kind == "configure" &&
	          !g_stub.events.back().flag,
	      "no prepare is recorded after a failed upscale and generation is configured off");

	// A repeated guest frame (pause/overlay redraw) records nothing.
	check(fsr.Record(command, Frame(6), true, error), "a new frame after the failure records");
	g_stub.events.clear();
	check(!fsr.Record(command, Frame(6), true, error) && !Find(g_stub.events, "upscale") &&
	          !Find(g_stub.events, "prepare"),
	      "a duplicate guest frame records no SDK work");

	// R3: the reset ring. Frame 7 resets (history was suspended by the duplicate).
	check(fsr.Record(command, Frame(7), true, error), "frame 7 records");
	g_stub.events.clear();
	uint64_t sequence = 0;
	for (uint64_t id = 8; id <= 14; ++id) {
		check(fsr.Record(command, Frame(id), true, error), "consecutive frames record");
		sequence = Find(g_stub.events, "prepare") ? g_stub.events.back().frame : sequence;
	}
	// Seven frames were configured after the resetting one; its slot is still intact.
	const uint64_t reset_frame = sequence - 7;
	check(Callback(reset_frame), "a reset survives seven newer configured frames");
	for (uint64_t frame = reset_frame + 1; frame <= sequence; ++frame) {
		check(!Callback(frame), "frames after the reset are not reset");
	}
	g_stub.events.clear();
	check(fsr.Record(command, Frame(15), true, error), "an eighth newer frame records");
	check(!Callback(reset_frame),
	      "the eighth newer frame reuses the slot: a callback delayed that long loses its reset");
	check(Callback(reset_frame, true), "an SDK-provided reset is always passed through");

	fsr.Shutdown();
	check(g_stub.live_contexts == 0 && !fsr.OwnsSwapchain(), "shutdown destroys every context");
	fsr.Shutdown();
	check(!fsr.Record(command, Frame(16), true, error) && !error.empty(),
	      "recording after shutdown falls back with a diagnostic");
	std::printf("FsrPresentationSdkTests: %d checks, %d failures; stub SDK only, no GPU\n", checks,
	            failures);
	return failures == 0 ? 0 : 1;
}
