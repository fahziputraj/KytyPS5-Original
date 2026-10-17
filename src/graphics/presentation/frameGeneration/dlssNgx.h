#pragma once

#include "graphics/presentation/frameGeneration/dlssBackend.h"

namespace Libs::Graphics::FrameGeneration {

struct DlssNgxRequirements {
	std::vector<std::string> instance_extensions, device_extensions;
};

// Query first with null Vulkan handles before instance creation, then with
// instance + physical device before logical-device creation. Enable the returned
// extensions, and report the actual enabled names through DlssDevice.
bool QueryDlssNgxRequirements(const DlssApplication& application, VkInstance instance,
                              VkPhysicalDevice physical, DlssNgxRequirements& requirements,
                              std::string& error);

// Available only to explicitly SDK-enabled targets. No implicit SDK download,
// runtime activation or emulator/launcher linkage.
std::unique_ptr<DlssSdk> CreateDlssNgxSdk();

} // namespace Libs::Graphics::FrameGeneration
