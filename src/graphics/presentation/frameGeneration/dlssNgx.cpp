#include "graphics/presentation/frameGeneration/dlssNgx.h"

#include <nvsdk_ngx_helpers_vk.h>

#include <algorithm>
#include <mutex>

namespace Libs::Graphics::FrameGeneration {
namespace {

// NGX documents its API as not thread safe. This prototype permits one active
// adapter so one owner cannot shut down NGX while another is recording.
std::mutex g_ngx_mutex;
bool g_ngx_active = false;

bool Result(NVSDK_NGX_Result result, const char* operation, std::string& error) {
	if (NVSDK_NGX_SUCCEED(result))
		return true;
	error = std::string(operation) + " failed (NGX " +
	        std::to_string(static_cast<uint32_t>(result)) + ")";
	return false;
}

struct Discovery {
	std::wstring feature_path;
	const wchar_t* path;
	NVSDK_NGX_FeatureCommonInfo common{};
	NVSDK_NGX_FeatureDiscoveryInfo info{};
	explicit Discovery(const DlssApplication& app)
	    : feature_path(app.feature_directory.wstring()), path(feature_path.c_str()) {
		common.PathListInfo = {&path, 1};
		info.SDKVersion = NVSDK_NGX_Version_API;
		info.FeatureID = NVSDK_NGX_Feature_SuperSampling;
		info.Identifier.IdentifierType = NVSDK_NGX_Application_Identifier_Type_Project_Id;
		info.Identifier.v.ProjectDesc = {app.project_id.c_str(), NVSDK_NGX_ENGINE_TYPE_CUSTOM,
		                                 app.engine_version.c_str()};
		info.ApplicationDataPath = app.data_directory.c_str();
		info.FeatureInfo = &common;
	}
};

bool Extensions(uint32_t count, const VkExtensionProperties* properties,
                std::vector<std::string>& names, std::string& error) {
	if (count > 256 || (count && !properties)) {
		error = "NGX returned an invalid extension list";
		return false;
	}
	for (uint32_t i = 0; i < count; ++i) {
		const auto& name = properties[i].extensionName;
		const auto* end = std::find(name, name + VK_MAX_EXTENSION_NAME_SIZE, '\0');
		if (end == name || end == name + VK_MAX_EXTENSION_NAME_SIZE) {
			error = "NGX returned an invalid extension name";
			return false;
		}
		names.emplace_back(name, end);
	}
	return true;
}

bool Requirements(const DlssApplication& application, VkInstance instance,
                  VkPhysicalDevice physical, DlssNgxRequirements& requirements,
                  std::string& error) {
	requirements = {};
	if (const auto* invalid = ValidateDlssApplication(application)) {
		error = invalid;
		return false;
	}
	if (static_cast<bool>(instance) != static_cast<bool>(physical)) {
		error = "NGX discovery requires both instance and physical device, or neither";
		return false;
	}
	Discovery discovery(application);
	uint32_t count = 0;
	VkExtensionProperties* properties = nullptr;
	if (!Result(NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements(&discovery.info, &count,
	                                                                     &properties),
	            "NGX instance-extension discovery", error) ||
	    !Extensions(count, properties, requirements.instance_extensions, error))
		return false;
	if (!instance)
		return true;
	NVSDK_NGX_FeatureRequirement supported{};
	if (!Result(NVSDK_NGX_VULKAN_GetFeatureRequirements(instance, physical, &discovery.info,
	                                                    &supported),
	            "NGX device-support query", error))
		return false;
	if (supported.FeatureSupported != NVSDK_NGX_FeatureSupportResult_Supported) {
		error = "NGX reports unsupported hardware/driver/OS (flags " +
		        std::to_string(static_cast<uint32_t>(supported.FeatureSupported)) + ")";
		return false;
	}
	count = 0;
	properties = nullptr;
	return Result(NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements(
	                  instance, physical, &discovery.info, &count, &properties),
	              "NGX device-extension discovery", error) &&
	       Extensions(count, properties, requirements.device_extensions, error);
}

bool Enabled(const std::vector<std::string>& required, const std::vector<std::string>& enabled,
             std::string& error) {
	for (const auto& name : required) {
		if (std::find(enabled.begin(), enabled.end(), name) == enabled.end()) {
			error = "DLSS needs extension enabled before Vulkan device creation: " + name;
			return false;
		}
	}
	return true;
}

NVSDK_NGX_PerfQuality_Value Quality(DlssQuality quality) {
	switch (quality) {
	case DlssQuality::Quality:
		return NVSDK_NGX_PerfQuality_Value_MaxQuality;
	case DlssQuality::Balanced:
		return NVSDK_NGX_PerfQuality_Value_Balanced;
	case DlssQuality::Performance:
		return NVSDK_NGX_PerfQuality_Value_MaxPerf;
	case DlssQuality::UltraPerformance:
		return NVSDK_NGX_PerfQuality_Value_UltraPerformance;
	case DlssQuality::Dlaa:
		return NVSDK_NGX_PerfQuality_Value_DLAA;
	}
	return NVSDK_NGX_PerfQuality_Value_MaxQuality; // validated before entry
}

NVSDK_NGX_Resource_VK Resource(const TemporalImage& image, const DlssBinding& binding,
                               bool output = false) {
	return NVSDK_NGX_Create_ImageView_Resource_VK(binding.view, image.image, binding.range,
	                                              image.format, image.width, image.height, output);
}

class NgxSdk final : public DlssSdk {
  public:
	~NgxSdk() override { Shutdown(); }
	bool Initialize(const DlssApplication& application, const DlssDevice& device,
	                const DlssOptions& options, std::string& error) override {
		std::lock_guard lock(g_ngx_mutex);
		ShutdownLocked();
		if (const auto* invalid = ValidateDlssOptions(options)) {
			error = invalid;
			return false;
		}
		if (g_ngx_active) {
			error = "Another DLSS NGX adapter is already active";
			return false;
		}
		if (!device.instance || !device.physical || !device.device || !device.get_instance_proc ||
		    !device.get_device_proc) {
			error = "NGX requires valid Vulkan handles and entry points";
			return false;
		}
		const auto properties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
		    device.get_instance_proc(device.instance, "vkGetPhysicalDeviceProperties"));
		const auto wait_idle = reinterpret_cast<PFN_vkDeviceWaitIdle>(
		    device.get_device_proc(device.device, "vkDeviceWaitIdle"));
		if (!properties || !wait_idle) {
			error = "NGX Vulkan properties/wait-idle entry points are unavailable";
			return false;
		}
		VkPhysicalDeviceProperties actual{};
		properties(device.physical, &actual);
		if (actual.vendorID != 0x10de) {
			error = "NGX physical device is not an NVIDIA GPU";
			return false;
		}
		DlssNgxRequirements requirements;
		if (!Requirements(application, device.instance, device.physical, requirements, error) ||
		    !Enabled(requirements.instance_extensions, device.instance_extensions, error) ||
		    !Enabled(requirements.device_extensions, device.device_extensions, error))
			return false;
		m_application = application;
		m_discovery = std::make_unique<Discovery>(m_application);
		m_device = device.device;
		m_wait_idle = wait_idle;
		g_ngx_active = m_lease = true;
		if (!Result(NVSDK_NGX_VULKAN_Init_with_ProjectID(
		                application.project_id.c_str(), NVSDK_NGX_ENGINE_TYPE_CUSTOM,
		                application.engine_version.c_str(), application.data_directory.c_str(),
		                device.instance, device.physical, device.device, device.get_instance_proc,
		                device.get_device_proc, &m_discovery->common, NVSDK_NGX_Version_API),
		            "NGX Vulkan initialization", error))
			return false;
		m_initialized = true;
		if (!Result(NVSDK_NGX_VULKAN_GetCapabilityParameters(&m_parameters), "NGX capabilities",
		            error) ||
		    !m_parameters) {
			if (error.empty())
				error = "NGX returned no capability parameters";
			return false;
		}
		int supported = 0, needs_driver = 0;
		if (!Result(NVSDK_NGX_Parameter_GetI(
		                m_parameters, NVSDK_NGX_Parameter_SuperSampling_Available, &supported),
		            "NGX DLSS availability", error) ||
		    !supported) {
			if (error.empty())
				error = "DLSS Super Resolution is unavailable";
			return false;
		}
		if (!Result(NVSDK_NGX_Parameter_GetI(m_parameters,
		                                     NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver,
		                                     &needs_driver),
		            "NGX driver compatibility", error))
			return false;
		if (needs_driver) {
			error = "DLSS requires an updated NVIDIA driver";
			return false;
		}
		return CheckSettings(options, error);
	}

	bool Create(VkCommandBuffer command, const DlssOptions& options, std::string& error) override {
		std::lock_guard lock(g_ngx_mutex);
		NVSDK_NGX_DLSS_Create_Params create{};
		create.Feature = {options.render_width, options.render_height, options.output_width,
		                  options.output_height, Quality(options.quality)};
		create.InFeatureCreateFlags =
		    NVSDK_NGX_DLSS_Feature_Flags_MVLowRes | NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
		const auto result =
		    NGX_VULKAN_CREATE_DLSS_EXT1(m_device, command, 1, 1, &m_feature, m_parameters, &create);
		if (!Result(result, "NGX DLSS feature creation", error) || !m_feature) {
			if (m_feature) {
				NVSDK_NGX_VULKAN_ReleaseFeature(m_feature);
				m_feature = nullptr;
			}
			if (error.empty())
				error = "NGX returned no DLSS feature handle";
			return false;
		}
		return true;
	}

	bool Evaluate(VkCommandBuffer command, const DlssFrame& frame, bool reset,
	              std::string& error) override {
		std::lock_guard lock(g_ngx_mutex);
		const auto& input = frame.temporal;
		auto color = Resource(input.color, frame.color);
		auto depth = Resource(input.depth, frame.depth);
		auto motion = Resource(input.motion, frame.motion);
		auto output = Resource(input.output, frame.output, true);
		NVSDK_NGX_VK_DLSS_Eval_Params evaluate{};
		evaluate.Feature.pInColor = &color;
		evaluate.Feature.pInOutput = &output;
		evaluate.pInDepth = &depth;
		evaluate.pInMotionVectors = &motion;
		evaluate.InJitterOffsetX = input.jitter_x;
		evaluate.InJitterOffsetY = input.jitter_y;
		evaluate.InMVScaleX = input.motion_scale_x;
		evaluate.InMVScaleY = input.motion_scale_y;
		evaluate.InRenderSubrectDimensions = {input.color.width, input.color.height};
		evaluate.InReset = reset ? 1 : 0;
		evaluate.InPreExposure = evaluate.InExposureScale = 1.0f;
		evaluate.InFrameTimeDeltaInMsec = input.milliseconds;
		return Result(NGX_VULKAN_EVALUATE_DLSS_EXT(command, m_feature, m_parameters, &evaluate),
		              "NGX DLSS evaluation", error);
	}

	void Shutdown() override {
		std::lock_guard lock(g_ngx_mutex);
		ShutdownLocked();
	}

  private:
	bool CheckSettings(const DlssOptions& options, std::string& error) {
		void* callback = nullptr;
		if (!Result(NVSDK_NGX_Parameter_GetVoidPointer(
		                m_parameters, NVSDK_NGX_Parameter_DLSSOptimalSettingsCallback, &callback),
		            "NGX optimal-settings callback", error) ||
		    !callback) {
			if (error.empty())
				error = "NGX has no DLSS optimal-settings callback";
			return false;
		}
		NVSDK_NGX_Parameter_SetUI(m_parameters, NVSDK_NGX_Parameter_Width, options.output_width);
		NVSDK_NGX_Parameter_SetUI(m_parameters, NVSDK_NGX_Parameter_Height, options.output_height);
		NVSDK_NGX_Parameter_SetI(m_parameters, NVSDK_NGX_Parameter_PerfQualityValue,
		                         Quality(options.quality));
		NVSDK_NGX_Parameter_SetI(m_parameters, NVSDK_NGX_Parameter_RTXValue, 0);
		if (!Result(reinterpret_cast<PFN_NVSDK_NGX_DLSS_GetOptimalSettingsCallback>(callback)(
		                m_parameters),
		            "NGX optimal-settings query", error))
			return false;
		unsigned width = 0, height = 0;
		if (!Result(NVSDK_NGX_Parameter_GetUI(m_parameters, NVSDK_NGX_Parameter_OutWidth, &width),
		            "NGX optimal width", error) ||
		    !Result(NVSDK_NGX_Parameter_GetUI(m_parameters, NVSDK_NGX_Parameter_OutHeight, &height),
		            "NGX optimal height", error))
			return false;
		unsigned min_w = width, max_w = width, min_h = height, max_h = height;
		// Older runtimes expose only the optimal dimensions; preserve those as a
		// conservative fixed-size range if dynamic-range queries are unavailable.
		const auto optional = [&](const char* name, unsigned& value) {
			unsigned queried = value;
			if (NVSDK_NGX_SUCCEED(NVSDK_NGX_Parameter_GetUI(m_parameters, name, &queried)))
				value = queried;
		};
		optional(NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Min_Render_Width, min_w);
		optional(NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Max_Render_Width, max_w);
		optional(NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Min_Render_Height, min_h);
		optional(NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Max_Render_Height, max_h);
		if (!min_w || !min_h || min_w > max_w || min_h > max_h || max_w > options.output_width ||
		    max_h > options.output_height || options.render_width < min_w ||
		    options.render_width > max_w || options.render_height < min_h ||
		    options.render_height > max_h) {
			error = "Requested DLSS render size is outside the SDK's supported quality range";
			return false;
		}
		return true;
	}

	void ShutdownLocked() {
		if (m_feature) {
			m_wait_idle(m_device);
			NVSDK_NGX_VULKAN_ReleaseFeature(m_feature);
		}
		if (m_parameters)
			NVSDK_NGX_VULKAN_DestroyParameters(m_parameters);
		if (m_initialized)
			NVSDK_NGX_VULKAN_Shutdown1(m_device);
		m_feature = nullptr;
		m_parameters = nullptr;
		m_device = VK_NULL_HANDLE;
		m_wait_idle = nullptr;
		m_initialized = false;
		m_discovery.reset();
		if (m_lease)
			g_ngx_active = false;
		m_lease = false;
	}

	DlssApplication m_application;
	std::unique_ptr<Discovery> m_discovery;
	VkDevice m_device = VK_NULL_HANDLE;
	PFN_vkDeviceWaitIdle m_wait_idle = nullptr;
	NVSDK_NGX_Parameter* m_parameters = nullptr;
	NVSDK_NGX_Handle* m_feature = nullptr;
	bool m_initialized = false, m_lease = false;
};
} // namespace

bool QueryDlssNgxRequirements(const DlssApplication& application, VkInstance instance,
                              VkPhysicalDevice physical, DlssNgxRequirements& requirements,
                              std::string& error) {
	std::lock_guard lock(g_ngx_mutex);
	error.clear();
	return Requirements(application, instance, physical, requirements, error);
}

std::unique_ptr<DlssSdk> CreateDlssNgxSdk() { return std::make_unique<NgxSdk>(); }

} // namespace Libs::Graphics::FrameGeneration
