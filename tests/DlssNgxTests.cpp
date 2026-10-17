// Exercise the actual NGX adapter against SDK declarations and stub symbols.
// No NVIDIA library, feature DLL, Vulkan loader or GPU is used in this target.
#include "DlssTestFixtures.h"
#include "graphics/presentation/frameGeneration/dlssNgx.h"

#include <nvsdk_ngx_vk.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <map>

using namespace DlssTests;

struct FakeParameters {
	std::map<std::string, unsigned> ui;
	std::map<std::string, int> integers;
	std::map<std::string, float> floats;
	std::map<std::string, void*> pointers;
};

namespace {
FakeParameters& Params(NVSDK_NGX_Parameter* params) {
	return *reinterpret_cast<FakeParameters*>(params);
}
const FakeParameters& Params(const NVSDK_NGX_Parameter* params) {
	return *reinterpret_cast<const FakeParameters*>(params);
}
enum class Fault {
	None,
	Discovery,
	Unsupported,
	Init,
	Capabilities,
	Availability,
	Driver,
	Callback,
	Settings,
	Create,
	EmptyFeature,
	Evaluate
};
struct State {
	Fault fault = Fault::None;
	uint32_t vendor = 0x10de;
	bool no_properties = false, no_wait = false;
	int init = 0, creates = 0, evaluations = 0, destroyed = 0, shutdowns = 0, releases = 0,
	    waits = 0;
	std::string project;
	std::vector<std::string> events;
	std::array<NVSDK_NGX_Resource_VK, 4> resources{};
	int reset = -1, create_flags = 0, quality = -1;
	unsigned render_width = 0, output_width = 0;
	float jitter_x = 0, jitter_y = 0, mv_x = 0, mv_y = 0, milliseconds = 0;
} g_state;

constexpr auto Success = NVSDK_NGX_Result_Success;
constexpr auto Failure = NVSDK_NGX_Result_FAIL_InvalidParameter;

NVSDK_NGX_Result NVSDK_CONV Optimal(NVSDK_NGX_Parameter* params) {
	if (g_state.fault == Fault::Settings)
		return Failure;
	Params(params).ui[NVSDK_NGX_Parameter_OutWidth] = 1280;
	Params(params).ui[NVSDK_NGX_Parameter_OutHeight] = 720;
	return Success;
}

VKAPI_ATTR void VKAPI_CALL Properties(VkPhysicalDevice, VkPhysicalDeviceProperties* properties) {
	properties->vendorID = g_state.vendor;
}
VKAPI_ATTR VkResult VKAPI_CALL Wait(VkDevice) {
	++g_state.waits;
	g_state.events.push_back("wait");
	return VK_SUCCESS;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL InstanceProc(VkInstance, const char* name) {
	if (!g_state.no_properties && std::strcmp(name, "vkGetPhysicalDeviceProperties") == 0)
		return reinterpret_cast<PFN_vkVoidFunction>(Properties);
	return nullptr;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL DeviceProc(VkDevice, const char* name) {
	if (!g_state.no_wait && std::strcmp(name, "vkDeviceWaitIdle") == 0)
		return reinterpret_cast<PFN_vkVoidFunction>(Wait);
	return nullptr;
}
DlssDevice NgxDevice() {
	auto device = Device();
	device.get_instance_proc = InstanceProc;
	device.get_device_proc = DeviceProc;
	device.instance_extensions = {"VK_KHR_get_physical_device_properties2"};
	device.device_extensions = {"VK_KHR_push_descriptor"};
	return device;
}
} // namespace

extern "C" {
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements(
    const NVSDK_NGX_FeatureDiscoveryInfo*, uint32_t* count, VkExtensionProperties** properties) {
	static VkExtensionProperties extension{};
	std::snprintf(extension.extensionName, sizeof(extension.extensionName),
	              "VK_KHR_get_physical_device_properties2");
	*count = 1;
	*properties = &extension;
	return g_state.fault == Fault::Discovery ? Failure : Success;
}
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements(
    VkInstance, VkPhysicalDevice, const NVSDK_NGX_FeatureDiscoveryInfo*, uint32_t* count,
    VkExtensionProperties** properties) {
	static VkExtensionProperties extension{};
	std::snprintf(extension.extensionName, sizeof(extension.extensionName),
	              "VK_KHR_push_descriptor");
	*count = 1;
	*properties = &extension;
	return Success;
}
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_VULKAN_GetFeatureRequirements(
    VkInstance, VkPhysicalDevice, const NVSDK_NGX_FeatureDiscoveryInfo*,
    NVSDK_NGX_FeatureRequirement* requirement) {
	requirement->FeatureSupported = g_state.fault == Fault::Unsupported
	                                    ? NVSDK_NGX_FeatureSupportResult_AdapterUnsupported
	                                    : NVSDK_NGX_FeatureSupportResult_Supported;
	return Success;
}
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_VULKAN_Init_with_ProjectID(
    const char* project, NVSDK_NGX_EngineType, const char*, const wchar_t*, VkInstance,
    VkPhysicalDevice, VkDevice, PFN_vkGetInstanceProcAddr, PFN_vkGetDeviceProcAddr,
    const NVSDK_NGX_FeatureCommonInfo*, NVSDK_NGX_Version) {
	++g_state.init;
	g_state.project = project;
	return g_state.fault == Fault::Init ? Failure : Success;
}
NVSDK_NGX_Result NVSDK_CONV
NVSDK_NGX_VULKAN_GetCapabilityParameters(NVSDK_NGX_Parameter** parameters) {
	*parameters = reinterpret_cast<NVSDK_NGX_Parameter*>(new FakeParameters);
	return g_state.fault == Fault::Capabilities ? Failure : Success;
}
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_VULKAN_DestroyParameters(NVSDK_NGX_Parameter* parameters) {
	delete reinterpret_cast<FakeParameters*>(parameters);
	++g_state.destroyed;
	g_state.events.push_back("parameters");
	return Success;
}
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_VULKAN_Shutdown1(VkDevice) {
	++g_state.shutdowns;
	g_state.events.push_back("shutdown");
	return Success;
}
void NVSDK_CONV NVSDK_NGX_Parameter_SetUI(NVSDK_NGX_Parameter* params, const char* name,
                                          unsigned value) {
	Params(params).ui[name] = value;
}
void NVSDK_CONV NVSDK_NGX_Parameter_SetI(NVSDK_NGX_Parameter* params, const char* name, int value) {
	Params(params).integers[name] = value;
}
void NVSDK_CONV NVSDK_NGX_Parameter_SetF(NVSDK_NGX_Parameter* params, const char* name,
                                         float value) {
	Params(params).floats[name] = value;
}
void NVSDK_CONV NVSDK_NGX_Parameter_SetVoidPointer(NVSDK_NGX_Parameter* params, const char* name,
                                                   void* value) {
	Params(params).pointers[name] = value;
}
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_Parameter_GetUI(NVSDK_NGX_Parameter* params, const char* name,
                                                      unsigned* value) {
	const auto found = Params(params).ui.find(name);
	if (found == Params(params).ui.end())
		return Failure;
	*value = found->second;
	return Success;
}
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_Parameter_GetI(NVSDK_NGX_Parameter*, const char* name,
                                                     int* value) {
	if (std::strcmp(name, NVSDK_NGX_Parameter_SuperSampling_Available) == 0) {
		*value = g_state.fault == Fault::Availability ? 0 : 1;
		return Success;
	}
	if (std::strcmp(name, NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver) == 0) {
		*value = g_state.fault == Fault::Driver ? 1 : 0;
		return Success;
	}
	return Failure;
}
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_Parameter_GetVoidPointer(NVSDK_NGX_Parameter*,
                                                               const char* name, void** value) {
	if (std::strcmp(name, NVSDK_NGX_Parameter_DLSSOptimalSettingsCallback) != 0)
		return Failure;
	*value = g_state.fault == Fault::Callback ? nullptr : reinterpret_cast<void*>(Optimal);
	return Success;
}
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_VULKAN_CreateFeature1(VkDevice, VkCommandBuffer,
                                                            NVSDK_NGX_Feature,
                                                            NVSDK_NGX_Parameter* params,
                                                            NVSDK_NGX_Handle** feature) {
	++g_state.creates;
	g_state.render_width = Params(params).ui[NVSDK_NGX_Parameter_Width];
	g_state.output_width = Params(params).ui[NVSDK_NGX_Parameter_OutWidth];
	g_state.create_flags = Params(params).integers[NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags];
	g_state.quality = Params(params).integers[NVSDK_NGX_Parameter_PerfQualityValue];
	// A partial handle on failed creation must still be released by the adapter.
	*feature = g_state.fault == Fault::EmptyFeature ? nullptr : Handle<NVSDK_NGX_Handle*>(41);
	return g_state.fault == Fault::Create ? Failure : Success;
}
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_VULKAN_CreateFeature(VkCommandBuffer command,
                                                           NVSDK_NGX_Feature feature,
                                                           NVSDK_NGX_Parameter* params,
                                                           NVSDK_NGX_Handle** handle) {
	return NVSDK_NGX_VULKAN_CreateFeature1(VK_NULL_HANDLE, command, feature, params, handle);
}
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_VULKAN_ReleaseFeature(NVSDK_NGX_Handle*) {
	++g_state.releases;
	g_state.events.push_back("release");
	return Success;
}
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_VULKAN_EvaluateFeature_C(VkCommandBuffer,
                                                               const NVSDK_NGX_Handle*,
                                                               const NVSDK_NGX_Parameter* params,
                                                               PFN_NVSDK_NGX_ProgressCallback_C) {
	++g_state.evaluations;
	const std::array<const char*, 4> names{NVSDK_NGX_Parameter_Color, NVSDK_NGX_Parameter_Depth,
	                                       NVSDK_NGX_Parameter_MotionVectors,
	                                       NVSDK_NGX_Parameter_Output};
	for (size_t i = 0; i < names.size(); ++i)
		g_state.resources[i] =
		    *static_cast<NVSDK_NGX_Resource_VK*>(Params(params).pointers.at(names[i]));
	g_state.reset = Params(params).integers.at(NVSDK_NGX_Parameter_Reset);
	g_state.jitter_x = Params(params).floats.at(NVSDK_NGX_Parameter_Jitter_Offset_X);
	g_state.jitter_y = Params(params).floats.at(NVSDK_NGX_Parameter_Jitter_Offset_Y);
	g_state.mv_x = Params(params).floats.at(NVSDK_NGX_Parameter_MV_Scale_X);
	g_state.mv_y = Params(params).floats.at(NVSDK_NGX_Parameter_MV_Scale_Y);
	g_state.milliseconds = Params(params).floats.at(NVSDK_NGX_Parameter_FrameTimeDeltaInMsec);
	return g_state.fault == Fault::Evaluate ? Failure : Success;
}
} // extern "C"

int main() {
	int checks = 0, failures = 0;
	const auto check = [&](bool value, const char* name) {
		++checks;
		if (!value) {
			++failures;
			std::fprintf(stderr, "FAIL: %s\n", name);
		}
	};
	const auto application = Application();
	const auto options = Options();
	const auto device = NgxDevice();
	const auto command = Handle<VkCommandBuffer>(31);
	std::string error;
	DlssNgxRequirements requirements;
	check(QueryDlssNgxRequirements(application, VK_NULL_HANDLE, VK_NULL_HANDLE, requirements,
	                               error) &&
	          requirements.instance_extensions.size() == 1 &&
	          requirements.device_extensions.empty() && g_state.init == 0,
	      "pre-instance discovery never initializes SDK");
	check(!QueryDlssNgxRequirements(application, device.instance, VK_NULL_HANDLE, requirements,
	                                error),
	      "partial discovery handles rejected");
	check(QueryDlssNgxRequirements(application, device.instance, device.physical, requirements,
	                               error) &&
	          requirements.device_extensions.size() == 1,
	      "pre-device extension/support discovery");
	for (const auto fault :
	     {Fault::Discovery, Fault::Unsupported, Fault::Init, Fault::Capabilities,
	      Fault::Availability, Fault::Driver, Fault::Callback, Fault::Settings}) {
		g_state = {};
		g_state.fault = fault;
		{
			DlssBackend backend;
			check(!backend.Initialize(application, device, options, CreateDlssNgxSdk(), error) &&
			          !error.empty(),
			      "NGX capability/initialization failure falls back");
		}
		if (fault == Fault::Capabilities || fault == Fault::Availability ||
		    fault == Fault::Driver || fault == Fault::Callback || fault == Fault::Settings)
			check(g_state.destroyed == 1 && g_state.shutdowns == 1,
			      "partial parameter map and initialized SDK cleaned up exactly once");
		else
			check(g_state.shutdowns == 0,
			      "failed/unattempted SDK initialization is not shut down as successful");
	}
	for (int missing = 0; missing < 5; ++missing) {
		g_state = {};
		auto unsupported = device;
		if (missing == 0)
			g_state.vendor = 0x8086;
		if (missing == 1)
			g_state.no_properties = true;
		if (missing == 2)
			g_state.no_wait = true;
		if (missing == 3)
			unsupported.instance_extensions.clear();
		if (missing == 4)
			unsupported.device_extensions.clear();
		DlssBackend backend;
		check(!backend.Initialize(application, unsupported, options, CreateDlssNgxSdk(), error) &&
		          g_state.init == 0,
		      "real adapter rejects unsupported device/entry points/extensions before NGX init");
	}
	g_state = {};
	{
		DlssBackend backend;
		check(backend.Initialize(application, device, options, CreateDlssNgxSdk(), error) &&
		          g_state.project == application.project_id,
		      "application's own project identity reaches NGX");
		DlssBackend other;
		check(!other.Initialize(application, device, options, CreateDlssNgxSdk(), error) &&
		          g_state.init == 1,
		      "second adapter cannot share/shut down active NGX owner");
		auto frame = Frame();
		check(backend.Record(command, frame, error), "real adapter records through stub API");
		check(g_state.render_width == 1280 && g_state.output_width == 1920 &&
		          g_state.quality == NVSDK_NGX_PerfQuality_Value_MaxQuality,
		      "create dimensions and quality map to NGX");
		check(g_state.create_flags == (NVSDK_NGX_DLSS_Feature_Flags_MVLowRes |
		                               NVSDK_NGX_DLSS_Feature_Flags_AutoExposure),
		      "forward-Z low-res unjittered motion with auto exposure");
		check(g_state.reset == 1 && g_state.jitter_x == 0.25f && g_state.jitter_y == -0.125f &&
		          g_state.mv_x == 1280 && g_state.mv_y == -720,
		      "reset, pixel jitter and signed motion scale survive marshaling");
		check(g_state.milliseconds == frame.temporal.milliseconds,
		      "real frame duration reaches NGX");
		const std::array<const TemporalImage*, 4> images{
		    &frame.temporal.color, &frame.temporal.depth, &frame.temporal.motion,
		    &frame.temporal.output};
		const std::array<const DlssBinding*, 4> bindings{&frame.color, &frame.depth, &frame.motion,
		                                                 &frame.output};
		for (size_t i = 0; i < images.size(); ++i) {
			const auto& resource = g_state.resources[i];
			const auto& view = resource.Resource.ImageViewInfo;
			check(resource.Type == NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW &&
			          resource.ReadWrite == (i == 3) && view.Image == images[i]->image &&
			          view.ImageView == bindings[i]->view && view.Width == images[i]->width &&
			          view.Height == images[i]->height && view.Format == images[i]->format &&
			          view.SubresourceRange.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT,
			      "NGX receives matching image/view/format/extent and write access only on output");
		}
		++frame.temporal.id;
		check(backend.Record(command, frame, error) && g_state.reset == 0 && g_state.creates == 1,
		      "consecutive frame reuses feature without reset");
		g_state.fault = Fault::Evaluate;
		++frame.temporal.id;
		check(!backend.Record(command, frame, error), "NGX dispatch failure propagates");
		g_state.fault = Fault::None;
		check(!backend.Record(command, frame, error) && g_state.evaluations == 3,
		      "failed SDK recording cannot evaluate again without reinitialization");
	}
	check(g_state.events == std::vector<std::string>{"wait", "release", "parameters", "shutdown"},
	      "GPU completion precedes feature release, parameters and per-device shutdown");
	for (const auto fault : {Fault::Create, Fault::EmptyFeature}) {
		g_state = {};
		{
			DlssBackend backend;
			check(backend.Initialize(application, device, options, CreateDlssNgxSdk(), error),
			      "creation-failure test initializes");
			g_state.fault = fault;
			check(!backend.Record(command, Frame(), error) && g_state.evaluations == 0,
			      "failed/null feature cannot evaluate");
			g_state.fault = Fault::None;
			check(!backend.Record(command, Frame(), error),
			      "creation failure cannot reuse discarded initialization command");
			check(backend.Initialize(application, device, options, CreateDlssNgxSdk(), error) &&
			          backend.Record(command, Frame(), error) && g_state.reset == 1,
			      "explicit reinitialization recreates feature/history");
		}
		check(g_state.releases == (fault == Fault::Create ? 2 : 1) && g_state.waits == 1 &&
		          g_state.destroyed == 2 && g_state.shutdowns == 2,
		      "partial feature cleanup and final shutdown occur exactly once per resource");
	}
	std::printf("DlssNgxTests: %d checks, %d failures; stub NGX/Vulkan only\n", checks, failures);
	return failures ? 1 : 0;
}
