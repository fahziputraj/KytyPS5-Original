#ifndef FOCUS_GRID_H
#define FOCUS_GRID_H

#include <QHash>
#include <QString>
#include <QVector>

#include <optional>

// Which item has focus in a row of tiles or a grid of covers. Pure index arithmetic, no widgets:
// the pages translate arrow keys, the D-pad and the left stick into Move() and draw the ring.
class FocusGrid {
public:
	enum class Direction { Left, Right, Up, Down };

	// Items are numbered row by row. Current() is kept when it is still valid, otherwise it is
	// clamped into range; it is -1 for an empty grid. A column count below 1 counts as 1.
	void SetItems(int count, int columns);

	// -1 when the grid is empty.
	int Current() const { return m_current; }

	// Clamped into range; ignored (stays -1) for an empty grid.
	void SetCurrent(int index);

	// Left/Right step one item and cross row ends, but stop at the first and last item.
	// Up/Down step by the column count; Down from a row above a partial last row lands on that
	// row's last item. Returns false, leaving focus where it was, when the move would leave the
	// grid, so the caller can hand focus to another region.
	bool Move(Direction direction);

private:
	int m_count   = 0;
	int m_columns = 1;
	int m_current = -1;
};

// The pages visited so far, each with the item that had focus, so Back can restore both.
class PageHistory {
public:
	struct Entry {
		QString page;
		int     focus = 0;
	};

	// Records arriving on `page` with `focus`. Visiting the page that is already current only
	// refreshes its focus; it does not stack a second copy.
	void Visit(const QString& page, int focus);

	// Leaves the current page and returns the one before it with the focus it was left on.
	// Returns nothing, and keeps the current page, when there is no earlier page.
	std::optional<Entry> Back();

	// The last focus recorded for `page`, 0 for a page that was never visited.
	int FocusFor(const QString& page) const;

private:
	QVector<Entry>      m_stack;
	QHash<QString, int> m_last_focus;
};

#endif // FOCUS_GRID_H
