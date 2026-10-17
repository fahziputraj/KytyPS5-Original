#include "graphics/presentation/frameGeneration/fsrPresentation.h"

#include <cstdio>

using namespace Libs::Graphics::FrameGeneration;

namespace {
unsigned                      queries = 0;
PFN_vkVoidFunction VKAPI_CALL MissingEntryPoint(VkDevice, const char*) {
	++queries;
	return nullptr;
}
} // namespace

int main() {
	int        failures = 0;
	const auto check    = [&](bool ok, const char* name) {
		if (!ok) {
			std::fprintf(stderr, "FAIL: %s\n", name);
			++failures;
		}
	};
	// Fake handles are used only with the stub above. These early failures must
	// never load a GPU driver or the SDK library, or enter a device/SDK function.
	const auto  device   = reinterpret_cast<VkDevice>(uintptr_t(1));
	const auto  physical = reinterpret_cast<VkPhysicalDevice>(uintptr_t(2));
	const auto  library  = std::filesystem::absolute("missing-fsr-sdk.dll");
	std::string error;
	Fsr3Backend probe;
	check(!probe.Initialize(library, device, physical, MissingEntryPoint, 1920, 1080,
	                        VK_FORMAT_R8G8B8A8_UNORM, error) &&
	          queries == 1 && error.find("VK_KHR_get_memory_requirements2") != std::string::npos,
	      "offscreen backend rejects missing device entry point before loading SDK");
	queries = 0;
	check(!probe.Initialize(library, device, physical, MissingEntryPoint, 0, 1080,
	                        VK_FORMAT_R8G8B8A8_UNORM, error) &&
	          queries == 0,
	      "offscreen backend rejects invalid dimensions before querying device");
	check(!probe.Record(VK_NULL_HANDLE, {}, error) && !error.empty(),
	      "offscreen record without a context falls back with a diagnostic");
	probe.ResetHistory();
	probe.Shutdown();
	probe.Shutdown();
	FsrPresentation presentation;
	queries = 0;
	check(!presentation.Initialize(device, physical, MissingEntryPoint, 1920, 1080,
	                               VK_FORMAT_R8G8B8A8_UNORM, true, true, error) &&
	          queries == 1 && !error.empty() && !presentation.OwnsSwapchain(),
	      "presentation backend rejects unsupported device without replacing swapchain");
	queries = 0;
	check(!presentation.Initialize(device, physical, MissingEntryPoint, 20000, 1080,
	                               VK_FORMAT_R8G8B8A8_UNORM, true, true, error) &&
	          queries == 0,
	      "presentation backend rejects oversized dimensions before querying device");
	check(!presentation.Initialize(device, physical, MissingEntryPoint, 1920, 1080,
	                               VK_FORMAT_R16G16B16A16_SFLOAT, true, true, error) &&
	          queries == 0,
	      "presentation backend rejects unsupported HDR format before querying device");
	check(!presentation.Record(VK_NULL_HANDLE, {}, true, error) && !error.empty(),
	      "presentation record without a context falls back with a diagnostic");
	presentation.Suspend();
	presentation.WaitForPresents();
	presentation.Shutdown();
	presentation.Shutdown();
	check(!presentation.OwnsSwapchain(), "repeated fallback cleanup leaves no owned swapchain");
	std::printf("FSR backend CPU guard tests: %d failures\n", failures);
	return failures != 0;
}
