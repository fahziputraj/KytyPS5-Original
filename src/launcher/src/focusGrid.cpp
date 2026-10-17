#include "focusGrid.h"

#include <algorithm>

void FocusGrid::SetItems(int count, int columns) {
	m_count   = std::max(count, 0);
	m_columns = std::max(columns, 1);

	if (m_count == 0) {
		m_current = -1;
	} else if (m_current < 0) {
		m_current = 0;
	} else {
		m_current = std::min(m_current, m_count - 1);
	}
}

void FocusGrid::SetCurrent(int index) {
	m_current = (m_count == 0) ? -1 : std::clamp(index, 0, m_count - 1);
}

bool FocusGrid::Move(Direction direction) {
	if (m_current < 0) {
		return false;
	}

	int target = m_current;
	switch (direction) {
		case Direction::Left: target = m_current - 1; break;
		case Direction::Right: target = m_current + 1; break;
		case Direction::Up: target = m_current - m_columns; break;
		case Direction::Down: {
			const int last_row = (m_count - 1) / m_columns;
			if (m_current / m_columns >= last_row) {
				return false;
			}
			target = std::min(m_current + m_columns, m_count - 1);
			break;
		}
	}

	if (target < 0 || target >= m_count) {
		return false;
	}
	m_current = target;
	return true;
}

void PageHistory::Visit(const QString& page, int focus) {
	if (!m_stack.isEmpty() && m_stack.last().page == page) {
		m_stack.last().focus = focus;
	} else {
		m_stack.push_back({page, focus});
	}
	m_last_focus.insert(page, focus);
}

std::optional<PageHistory::Entry> PageHistory::Back() {
	if (m_stack.size() < 2) {
		return std::nullopt;
	}
	m_stack.pop_back();
	return m_stack.last();
}

int PageHistory::FocusFor(const QString& page) const {
	return m_last_focus.value(page, 0);
}
