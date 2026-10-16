#pragma once

#include <algorithm>
#include <cmath>

namespace Loader::GamePatch {

// An opt-in title patch supplies its own seconds-per-update. This controller
// follows elapsed frame time without stretching global guest clocks. Steps are
// bounded to the tested 60-to-30 FPS range; long pauses never create catch-up debt.
class SimulationClock {
public:
	static constexpr double Nominal = 1.0 / 60.0;
	static constexpr double Longest = 1.0 / 30.0;

	void Reset() { *this = {}; }

	[[nodiscard]] double Advance(double elapsed) {
		if (!std::isfinite(elapsed) || elapsed <= 0.0 || elapsed > 0.25) {
			Reset();
			return m_step;
		}
		// Account for the step used by the frame that just finished. Repay small
		// discrepancies gradually, avoiding alternating large/small physics steps.
		m_balance = std::clamp(m_balance + elapsed - m_step, -0.1, 0.1);
		m_average += (elapsed - m_average) * 0.2;
		m_step = std::clamp(m_average + m_balance * 0.25, Nominal, Longest);
		if (std::abs(m_average - Nominal) < 0.0003 && std::abs(m_balance) < 0.002) {
			m_step    = Nominal;
			m_balance = 0.0;
		}
		return m_step;
	}

private:
	double m_average = Nominal;
	double m_step    = Nominal;
	double m_balance = 0.0;
};

} // namespace Loader::GamePatch
