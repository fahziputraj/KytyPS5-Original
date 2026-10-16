#pragma once

#include <algorithm>
#include <cstdint>

namespace Libs::VideoOut {

// The guest display clock advances with elapsed host time, independently of how
// many frames the host can present. Missed ticks are coalesced, never spun through.
class VblankClock {
public:
	void Reset(uint64_t now, uint64_t frequency, uint32_t refresh) {
		m_epoch     = now;
		m_frequency = std::max<uint64_t>(frequency, 1);
		m_refresh   = std::max(refresh, 1u);
		m_count     = 0;
	}

	[[nodiscard]] uint64_t Advance(uint64_t now) {
		if (now < m_epoch) {
			return 0;
		}
		const auto elapsed = now - m_epoch;
		const auto count =
		    elapsed / m_frequency * m_refresh + elapsed % m_frequency * m_refresh / m_frequency;
		const auto ticks = count > m_count ? count - m_count : 0;
		m_count          = std::max(count, m_count);
		return ticks;
	}

	[[nodiscard]] uint64_t Deadline() const {
		const auto next = m_count + 1;
		return m_epoch + next / m_refresh * m_frequency +
		       (next % m_refresh * m_frequency + m_refresh - 1) / m_refresh;
	}

private:
	uint64_t m_epoch     = 0;
	uint64_t m_frequency = 1;
	uint64_t m_count     = 0;
	uint32_t m_refresh   = 60;
};

// A presentation limit changes only when a completed frame can be displayed.
// It does not scale the guest's monotonic clock or lower its vblank frequency.
[[nodiscard]] inline uint64_t PresentationSpacing(uint64_t frequency, uint32_t refresh,
                                                  uint32_t interval, uint32_t fps_limit = 0) {
	auto spacing =
	    (frequency * std::max(interval, 1u) + std::max(refresh, 1u) - 1) / std::max(refresh, 1u);
	if (fps_limit != 0) {
		spacing = std::max(spacing, (frequency + fps_limit - 1) / fps_limit);
	}
	return spacing;
}

[[nodiscard]] inline uint64_t PresentationDelay(uint64_t now, uint64_t previous, uint64_t frequency,
                                                uint32_t refresh, uint32_t interval,
                                                uint32_t fps_limit = 0) {
	if (previous == 0) {
		return 0;
	}
	const auto deadline = previous + PresentationSpacing(frequency, refresh, interval, fps_limit);
	return now >= deadline ? 0 : deadline - now;
}

// Keep the nominal deadline after a small scheduler overshoot, so each wake-up
// does not permanently add another millisecond to a 30 FPS frame period. A late
// CPU/GPU frame rebases the schedule instead of allowing a burst to catch up.
[[nodiscard]] inline uint64_t PresentationOrigin(uint64_t now, uint64_t previous,
                                                 uint64_t frequency, uint32_t refresh,
                                                 uint32_t interval, uint32_t fps_limit = 0) {
	if (previous != 0) {
		const auto deadline =
		    previous + PresentationSpacing(frequency, refresh, interval, fps_limit);
		if (now >= deadline && now - deadline <= frequency / 500) {
			return deadline;
		}
	}
	return now;
}

} // namespace Libs::VideoOut
