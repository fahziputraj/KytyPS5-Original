#pragma once

#include "common/emulatorConfig.h"
#include "graphics/host_gpu/renderer/image/image.h"
#include "graphics/presentation/frameGeneration/temporalCamera.h"

namespace Libs::Graphics::FrameGeneration {

inline bool Requested() {
	return Config::FsrFrameGenerationEnabled() || Config::GetFsrUpscale() != "off";
}

// PPSA21564 01.007 captures: RGBA16F MRT4.rg stores current UV minus
// previous UV, with (0,128) for surfaces requiring camera reconstruction.
// 07FA: 0xbf0..0xcfc -> MRT4 at 0xfa4. 5401: 0xbf8..0xd04 -> 0xfbc.
// The view block's current/previous VP matrices are at bytes 0/320.
inline bool CertifiedAstroProducer(uint64_t hash) {
	return hash == 0x07FA8F4328CBF91Aull || hash == 0x5401C2517A9B1B60ull;
}

struct SceneGuides {
	ImageId        motion;
	ImageId        depth;
	vk::Extent2D   extent {};
	TemporalCamera camera {};
	uint64_t       frame = UINT64_MAX;
};

} // namespace Libs::Graphics::FrameGeneration
