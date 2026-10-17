#include "DlssTestFixtures.h"

#include <cstdio>
#include <limits>

using namespace DlssTests;

namespace {
struct State {
	int initialize = 0, create = 0, evaluate = 0, shutdown = 0;
	bool fail_initialize = false, fail_create = false, fail_evaluate = false;
	std::vector<bool> resets;
};
class FakeSdk final : public DlssSdk {
  public:
	explicit FakeSdk(State& state) : state(state) {}
	bool Initialize(const DlssApplication&, const DlssDevice&, const DlssOptions&,
	                std::string& error) override {
		++state.initialize;
		if (state.fail_initialize) {
			error = "test: initialization failed";
			return false;
		}
		return true;
	}
	bool Create(VkCommandBuffer, const DlssOptions&, std::string& error) override {
		++state.create;
		if (state.fail_create) {
			error = "test: creation failed";
			return false;
		}
		return true;
	}
	bool Evaluate(VkCommandBuffer, const DlssFrame&, bool reset, std::string& error) override {
		++state.evaluate;
		state.resets.push_back(reset);
		if (state.fail_evaluate) {
			error = "test: evaluation failed";
			return false;
		}
		return true;
	}
	void Shutdown() override { ++state.shutdown; }
	State& state;
};
} // namespace

int main() {
	int checks = 0, failures = 0;
	const auto check = [&](bool value, const char* name) {
		++checks;
		if (!value) {
			++failures;
			std::fprintf(stderr, "FAIL: %s\n", name);
		}
	};
	auto application = Application();
	auto options = Options();
	check(!ValidateDlssApplication(application) && !ValidateDlssOptions(options) &&
	          !ValidateDlssFrame(Frame(), options),
	      "valid offscreen inputs");
	for (const auto* id : {"", "not-a-project-id", "86294c1e_06d4-45d1-91c1-40ae92ea4091",
	                       "z6294c1e-06d4-45d1-91c1-40ae92ea4091"}) {
		auto broken = application;
		broken.project_id = id;
		check(ValidateDlssApplication(broken), "invalid project identity rejected");
	}
	for (const auto field :
	     {&DlssApplication::data_directory, &DlssApplication::feature_directory}) {
		auto broken = application;
		broken.*field = "relative/path";
		check(ValidateDlssApplication(broken), "relative SDK path rejected");
	}
	for (const auto field : {&DlssOptions::render_width, &DlssOptions::render_height,
	                         &DlssOptions::output_width, &DlssOptions::output_height}) {
		auto broken = options;
		broken.*field = 0;
		check(ValidateDlssOptions(broken), "zero dimensions rejected");
		broken.*field = 16385;
		check(ValidateDlssOptions(broken), "oversized dimensions rejected");
	}
	auto dlaa = options;
	dlaa.quality = DlssQuality::Dlaa;
	check(ValidateDlssOptions(dlaa), "DLAA cannot upscale");
	dlaa.render_width = dlaa.output_width;
	dlaa.render_height = dlaa.output_height;
	check(!ValidateDlssOptions(dlaa), "native-size DLAA configuration");
	auto unknown = options;
	unknown.quality = static_cast<DlssQuality>(999);
	check(ValidateDlssOptions(unknown), "unknown quality rejected");
	for (const auto field :
	     {&DlssFrame::color, &DlssFrame::depth, &DlssFrame::motion, &DlssFrame::output}) {
		for (int invalid = 0; invalid < 9; ++invalid) {
			auto broken = Frame();
			auto& binding = broken.*field;
			switch (invalid) {
			case 0:
				binding.view = VK_NULL_HANDLE;
				break;
			case 1:
				binding.layout = VK_IMAGE_LAYOUT_UNDEFINED;
				break;
			case 2:
				binding.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
				break;
			case 3:
				binding.samples = VK_SAMPLE_COUNT_4_BIT;
				break;
			case 4:
				binding.range.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
				break;
			case 5:
				binding.range.baseMipLevel = 1;
				break;
			case 6:
				binding.range.levelCount = 2;
				break;
			case 7:
				binding.range.baseArrayLayer = 1;
				break;
			case 8:
				binding.range.layerCount = 2;
				break;
			}
			check(ValidateDlssFrame(broken, options), "invalid Vulkan binding rejected");
		}
	}
	for (const auto field : {&TemporalInputs::color, &TemporalInputs::depth,
	                         &TemporalInputs::motion, &TemporalInputs::output}) {
		for (int invalid = 0; invalid < 4; ++invalid) {
			auto broken = Frame();
			auto& image = broken.temporal.*field;
			switch (invalid) {
			case 0:
				image.image = VK_NULL_HANDLE;
				break;
			case 1:
				image.width += 1;
				break;
			case 2:
				image.height = 0;
				break;
			case 3:
				image.format = VK_FORMAT_UNDEFINED;
				break;
			}
			check(ValidateDlssFrame(broken, options), "invalid image descriptor rejected");
		}
	}
	for (const auto field :
	     {&TemporalInputs::color, &TemporalInputs::depth, &TemporalInputs::motion}) {
		auto broken = Frame();
		broken.temporal.output.image = (broken.temporal.*field).image;
		check(ValidateDlssFrame(broken, options), "output/input alias rejected");
	}
	auto broken = Frame();
	broken.output.view = broken.color.view;
	check(ValidateDlssFrame(broken, options), "view alias rejected");
	broken = Frame();
	broken.temporal.hudless_scene = false;
	check(ValidateDlssFrame(broken, options), "unverified/HUD-retaining color rejected");
	broken = Frame();
	broken.temporal.jitter_x = std::numeric_limits<float>::quiet_NaN();
	check(ValidateDlssFrame(broken, options), "nonfinite jitter rejected");
	broken = Frame();
	broken.temporal.milliseconds = 101;
	check(ValidateDlssFrame(broken, options), "long frame gap rejected");

	std::string error;
	const auto command = Handle<VkCommandBuffer>(30);
	State state;
	DlssBackend backend;
	check(!backend.Record(command, Frame(), error) && !error.empty(), "uninitialized fallback");
	auto device = Device();
	device.vendor_id = 0x8086;
	check(!backend.Initialize(application, device, options, std::make_unique<FakeSdk>(state),
	                          error) &&
	          state.initialize == 0,
	      "Intel rejected before SDK access");
	device = Device();
	device.device = VK_NULL_HANDLE;
	check(!backend.Initialize(application, device, options, std::make_unique<FakeSdk>(state),
	                          error) &&
	          state.initialize == 0,
	      "missing device rejected before SDK access");
	check(!backend.Initialize(application, Device(), options, nullptr, error),
	      "missing optional SDK fallback");
	state.fail_initialize = true;
	check(!backend.Initialize(application, Device(), options, std::make_unique<FakeSdk>(state),
	                          error) &&
	          state.shutdown == 1,
	      "partial initialization is cleaned up");
	state.fail_initialize = false;
	error = "stale";
	check(backend.Initialize(application, Device(), options, std::make_unique<FakeSdk>(state),
	                         error) &&
	          error.empty(),
	      "valid initialization clears diagnostics");
	auto frame = Frame();
	check(!backend.Record(VK_NULL_HANDLE, frame, error) && state.create == 0,
	      "null command cannot create feature");
	broken = frame;
	broken.output.view = VK_NULL_HANDLE;
	check(!backend.Record(command, broken, error) && state.create == 0,
	      "invalid resource cannot reach SDK");
	state.fail_create = true;
	check(!backend.Record(command, frame, error) && state.evaluate == 0,
	      "creation failure does not evaluate or commit history");
	state.fail_create = false;
	check(!backend.Record(command, frame, error) && state.create == 1,
	      "failed SDK recording cannot reuse a discarded initialization command");
	check(
	    backend.Initialize(application, Device(), options, std::make_unique<FakeSdk>(state), error),
	    "explicit reinitialization after create failure");
	check(backend.Record(command, frame, error) && state.resets.back(),
	      "first successful frame resets history");
	const auto evaluations = state.evaluate;
	check(!backend.Record(command, frame, error) && state.evaluate == evaluations,
	      "duplicate cannot advance SDK history");
	++frame.temporal.id;
	check(backend.Record(command, frame, error) && state.resets.back(),
	      "frame after rejected duplicate resets history");
	++frame.temporal.id;
	check(backend.Record(command, frame, error) && !state.resets.back(),
	      "consecutive real frame retains history");
	frame.temporal.id += 2;
	check(backend.Record(command, frame, error) && state.resets.back(),
	      "skipped ID resets history");
	frame.temporal.id -= 3;
	check(backend.Record(command, frame, error) && state.resets.back(),
	      "rewound ID resets history");
	++frame.temporal.id;
	frame.temporal.reset = true;
	check(backend.Record(command, frame, error) && state.resets.back(),
	      "explicit camera cut resets history");
	++frame.temporal.id;
	frame.temporal.reset = false;
	state.fail_evaluate = true;
	check(!backend.Record(command, frame, error), "SDK evaluation failure falls back");
	state.fail_evaluate = false;
	check(!backend.Record(command, frame, error),
	      "evaluation failure requires reinitialization before retry");
	check(backend.Initialize(application, Device(), options, std::make_unique<FakeSdk>(state),
	                         error) &&
	          backend.Record(command, frame, error) && state.resets.back(),
	      "failed frame ID can retry after reinitialization with reset");
	backend.ResetHistory();
	++frame.temporal.id;
	check(backend.Record(command, frame, error) && state.resets.back(),
	      "resume after suspension resets");
	frame.temporal.id = std::numeric_limits<uint64_t>::max();
	check(backend.Record(command, frame, error) && state.resets.back(), "last uint64 frame ID");
	frame.temporal.id = 0;
	check(backend.Record(command, frame, error) && state.resets.back(),
	      "frame ID wrap resets history");
	backend.Shutdown();
	const auto shutdowns = state.shutdown;
	backend.Shutdown();
	check(state.shutdown == shutdowns && !backend.Record(command, frame, error),
	      "idempotent shutdown and original-image fallback");
	std::printf("DlssBackendTests: %d checks, %d failures; CPU only, no GPU/NGX runtime\n", checks,
	            failures);
	return failures ? 1 : 0;
}
