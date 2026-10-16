#pragma once

#include <algorithm>
#include <cmath>

namespace Loader::GamePatch {

// An opt-in title patch supplies its own seconds-per-update. This controller
// follows elapsed frame time without stretching global guest clocks. Steps are
// bounded to the configured 60-to-30 FPS range; long pauses never create catch-up debt.
class SimulationClock {
public:
	static constexpr double Nominal       = 1.0 / 60.0;
	static constexpr double Longest       = 1.0 / 30.0;
	static constexpr double MaxStepChange = 0.0015;

	void Reset() { *this = {}; }

	[[nodiscard]] double Advance(double elapsed) {
		if (!std::isfinite(elapsed) || elapsed <= 0.0 || elapsed > 0.25) {
			Reset();
			return m_step;
		}
		// Filter the frame period over real time, keeping isolated stalls out of
		// the estimate. Account for their elapsed time separately and repay it
		// over 600 ms rather than correcting a quarter of the debt every frame.
		m_balance         = std::clamp(m_balance + elapsed - m_step, -0.25, 0.25);
		const auto period = std::clamp(elapsed, Nominal, Longest);
		const auto alpha  = -std::expm1(-period / 0.08);
		m_average += (period - m_average) * alpha;
		const auto correction = std::clamp(m_balance * period / 0.6, -0.001, 0.001);
		const auto target     = std::clamp(m_average + correction, Nominal, Longest);
		m_step += std::clamp(target - m_step, -MaxStepChange, MaxStepChange);
		return m_step;
	}

private:
	double m_average = Nominal;
	double m_step    = Nominal;
	double m_balance = 0.0;
};

} // namespace Loader::GamePatch
