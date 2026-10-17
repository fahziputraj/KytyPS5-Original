#include "graphics/presentation/frameGeneration/dlssNgx.h"

#include <cstdio>

using namespace Libs::Graphics::FrameGeneration;

int main() {
	// The factory references the complete adapter and forces a real NGX link.
	// Invalid identity is rejected before any SDK call. Never initialize NGX,
	// enumerate hardware, load a feature DLL or submit GPU work in this check.
	auto sdk = CreateDlssNgxSdk();
	std::string error;
	DlssNgxRequirements requirements;
	if (!sdk || QueryDlssNgxRequirements({}, VK_NULL_HANDLE, VK_NULL_HANDLE, requirements, error) ||
	    error.empty())
		return 1;
	std::puts("DLSS NGX adapter linked successfully. GPU initialization/evaluation NOT RUN; "
	          "in-game integration disabled.");
	return 0;
}
