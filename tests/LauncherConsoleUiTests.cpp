#include "consoleTheme.h"
#include "focusGrid.h"

#include <QApplication>
#include <QColor>
#include <QLineEdit>
#include <QList>
#include <QPixmap>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <cmath>
#include <cstdio>
#include <optional>

namespace {

QStringList g_log_messages;

void CaptureLogMessage(QtMsgType /*type*/, const QMessageLogContext& /*context*/,
                       const QString& message) {
	g_log_messages.append(message);
}

template <class Check>
void TestTokenContrast(Check& check) {
	const ConsoleTheme::Tokens& t = ConsoleTheme::Get();

	const QList<QColor> surfaces {t.bg, t.rail, t.surface1, t.surface2, t.hero_base, t.hero_accent};
	for (const QColor& surface: surfaces) {
		check(ConsoleTheme::ContrastRatio(t.text_primary, surface) >= 4.5,
		      "text-primary keeps 4.5:1 on every surface");
		check(ConsoleTheme::ContrastRatio(t.text_secondary, surface) >= 4.5,
		      "text-secondary keeps 4.5:1 on every surface");
		check(ConsoleTheme::ContrastRatio(t.focus, surface) >= 3.0,
		      "the focus ring keeps 3:1 on every surface");
	}

	// Muted text is for placeholders and captions on the flat surfaces only.
	const QList<QColor> flat_surfaces {t.bg, t.rail, t.surface1};
	for (const QColor& surface: flat_surfaces) {
		check(ConsoleTheme::ContrastRatio(t.text_muted, surface) >= 4.5,
		      "text-muted keeps 4.5:1 on the flat surfaces");
	}

	// Primary button: bg-coloured text on a white fill.
	check(ConsoleTheme::ContrastRatio(t.bg, Qt::white) >= 4.5,
	      "primary button text keeps 4.5:1 on white");

	// The WCAG formula itself: the extreme pair is 21:1 and the order does not matter.
	const double extreme = ConsoleTheme::ContrastRatio(Qt::black, Qt::white);
	check(std::fabs(extreme - 21.0) < 0.01, "black on white is 21:1");
	check(std::fabs(ConsoleTheme::ContrastRatio(Qt::white, Qt::black) - extreme) < 1e-9,
	      "contrast ratio is symmetric");
	check(std::fabs(ConsoleTheme::ContrastRatio(t.bg, t.bg) - 1.0) < 1e-9,
	      "a colour on itself is 1:1");
}

template <class Check>
void TestFontsAndStyleSheet(Check& check) {
	check(ConsoleTheme::FontFamily() == QStringLiteral("Segoe UI"),
	      "before loading, the font family falls back to Segoe UI");
	check(ConsoleTheme::LoadFonts(), "the bundled DM Sans loads from the resources");
	check(ConsoleTheme::FontFamily() == QStringLiteral("DM Sans"),
	      "the registered family is DM Sans");
	check(ConsoleTheme::LoadFonts(), "loading the font twice is harmless");

	const QString style_sheet = ConsoleTheme::StyleSheet();
	check(style_sheet.contains(QStringLiteral("#0A1020")), "the style sheet carries the bg token");
	check(style_sheet.contains(QStringLiteral("#070C18")),
	      "the style sheet carries the rail token");
	check(!style_sheet.contains(QLatin1Char('@')), "every @token@ placeholder is replaced");
}

template <class Check>
void TestStyleSheetParses(Check& check) {
	QWidget page;
	page.setObjectName(QStringLiteral("consolePage"));
	auto* rail = new QWidget(&page);
	rail->setObjectName(QStringLiteral("consoleRail"));
	auto* layout = new QVBoxLayout(&page);
	auto* search = new QLineEdit(&page);
	search->setObjectName(QStringLiteral("search"));
	search->setPlaceholderText(QStringLiteral("Search"));
	auto* pill = new QPushButton(QStringLiteral("Pill"), &page);
	pill->setProperty("pill", true);
	auto* primary = new QPushButton(QStringLiteral("Play"), &page);
	primary->setProperty("primary", true);
	auto* rail_button = new QToolButton(&page);
	rail_button->setText(QStringLiteral("Home"));
	rail_button->setCheckable(true);
	rail_button->setChecked(true);
	rail_button->setProperty("rail", true);
	for (QWidget* widget:
	     {static_cast<QWidget*>(rail), static_cast<QWidget*>(search), static_cast<QWidget*>(pill),
	      static_cast<QWidget*>(primary), static_cast<QWidget*>(rail_button)}) {
		layout->addWidget(widget);
	}

	g_log_messages.clear();
	const QtMessageHandler previous = qInstallMessageHandler(CaptureLogMessage);
	page.setStyleSheet(ConsoleTheme::StyleSheet());
	page.ensurePolished();
	page.resize(320, 360);
	const QPixmap rendered = page.grab(); // resolves and paints every rule
	qInstallMessageHandler(previous);

	for (const QString& message: g_log_messages) {
		std::fprintf(stderr, "style sheet message: %s\n", qPrintable(message));
	}
	check(g_log_messages.isEmpty(), "the style sheet parses and applies without Qt warnings");
	check(!rendered.isNull(), "a page with the style sheet renders");
	check(pill->minimumSizeHint().height() >= 44 && primary->minimumSizeHint().height() >= 44 &&
	          search->minimumSizeHint().height() >= 44,
	      "pill buttons and the search field are at least 44 px tall");
}

template <class Check>
void TestFocusGrid(Check& check) {
	using Direction = FocusGrid::Direction;

	// 7 items in 3 columns: rows {0,1,2} {3,4,5} {6}.
	FocusGrid grid;
	grid.SetItems(7, 3);
	check(grid.Current() == 0, "a new grid focuses the first item");

	grid.SetCurrent(2);
	check(grid.Move(Direction::Right) && grid.Current() == 3,
	      "Right from the end of a row crosses to the next row");
	check(grid.Move(Direction::Left) && grid.Current() == 2,
	      "Left from the start of a row crosses to the previous row");

	grid.SetCurrent(4);
	check(grid.Move(Direction::Down) && grid.Current() == 6,
	      "Down from 4 clamps to the last item of the partial last row");
	grid.SetCurrent(5);
	check(grid.Move(Direction::Down) && grid.Current() == 6,
	      "Down from 5 clamps to the last item of the partial last row");
	grid.SetCurrent(3);
	check(grid.Move(Direction::Down) && grid.Current() == 6,
	      "Down from 3 clamps to the last item of the partial last row");
	check(!grid.Move(Direction::Down) && grid.Current() == 6,
	      "Down from the last row returns false and keeps focus");
	check(!grid.Move(Direction::Right) && grid.Current() == 6,
	      "Right at the last item returns false and keeps focus");

	grid.SetCurrent(1);
	check(!grid.Move(Direction::Up) && grid.Current() == 1,
	      "Up from the first row returns false and keeps focus");
	grid.SetCurrent(5);
	check(grid.Move(Direction::Up) && grid.Current() == 2, "Up steps back by one row");
	grid.SetCurrent(6);
	check(grid.Move(Direction::Up) && grid.Current() == 3, "Up from a partial last row steps back");

	grid.SetCurrent(0);
	check(!grid.Move(Direction::Left) && grid.Current() == 0,
	      "Left at the first item returns false and keeps focus");

	// A single row of 5.
	FocusGrid row;
	row.SetItems(5, 5);
	check(!row.Move(Direction::Left) && row.Current() == 0, "Left from 0 in a single row is false");
	check(!row.Move(Direction::Up) && !row.Move(Direction::Down),
	      "a single row has nothing above or below");
	for (int i = 0; i < 4; i++) {
		row.Move(Direction::Right);
	}
	check(row.Current() == 4, "Right walks a single row to its last item");
	check(!row.Move(Direction::Right) && row.Current() == 4, "Right at the end of a row is false");

	// More columns than items: one short row.
	FocusGrid wide;
	wide.SetItems(2, 5);
	check(!wide.Move(Direction::Down) && !wide.Move(Direction::Up),
	      "a row shorter than the column count has no row above or below");
	check(wide.Move(Direction::Right) && wide.Current() == 1 && !wide.Move(Direction::Right),
	      "a short row stops at its last item");

	// Empty grid.
	FocusGrid empty;
	empty.SetItems(0, 3);
	check(empty.Current() == -1, "an empty grid has no focus");
	check(!empty.Move(Direction::Left) && !empty.Move(Direction::Right) &&
	          !empty.Move(Direction::Up) && !empty.Move(Direction::Down),
	      "every move in an empty grid returns false");
	check(empty.Current() == -1, "moving in an empty grid keeps it empty");
	empty.SetCurrent(2);
	check(empty.Current() == -1, "SetCurrent on an empty grid stays at -1");

	// SetItems keeps a valid focus and clamps an invalid one.
	FocusGrid keep;
	keep.SetItems(7, 3);
	keep.SetCurrent(4);
	keep.SetItems(7, 2);
	check(keep.Current() == 4, "SetItems keeps the focus when it is still valid");
	keep.SetItems(3, 3);
	check(keep.Current() == 2, "SetItems clamps the focus to the new last item");
	keep.SetItems(0, 3);
	check(keep.Current() == -1, "SetItems(0) clears the focus");
	keep.SetItems(4, 2);
	check(keep.Current() == 0, "SetItems after empty focuses the first item");

	// SetCurrent clamps out-of-range requests.
	FocusGrid clamp;
	clamp.SetItems(4, 2);
	clamp.SetCurrent(99);
	check(clamp.Current() == 3, "SetCurrent clamps above the last item");
	clamp.SetCurrent(-5);
	check(clamp.Current() == 0, "SetCurrent clamps below the first item");

	// A bad column count is treated as a single column.
	FocusGrid column;
	column.SetItems(3, 0);
	check(column.Move(Direction::Down) && column.Current() == 1,
	      "zero columns behaves as one column for Down");
	check(column.Move(Direction::Up) && column.Current() == 0, "and for Up");
}

template <class Check>
void TestPageHistory(Check& check) {
	PageHistory history;
	check(!history.Back().has_value(), "an empty history has nowhere to go back to");
	check(history.FocusFor(QStringLiteral("home")) == 0, "an unseen page remembers focus 0");

	history.Visit(QStringLiteral("home"), 2);
	history.Visit(QStringLiteral("library"), 5);
	history.Visit(QStringLiteral("settings"), 0);
	check(history.FocusFor(QStringLiteral("library")) == 5, "FocusFor returns the recorded focus");
	check(history.FocusFor(QStringLiteral("home")) == 2, "FocusFor works for the first page too");
	check(history.FocusFor(QStringLiteral("about")) == 0, "FocusFor is 0 for a page never visited");

	const std::optional<PageHistory::Entry> first = history.Back();
	check(first.has_value() && first->page == QStringLiteral("library") && first->focus == 5,
	      "Back returns the previous page with its focus");
	const std::optional<PageHistory::Entry> second = history.Back();
	check(second.has_value() && second->page == QStringLiteral("home") && second->focus == 2,
	      "Back keeps walking toward the first page");
	check(!history.Back().has_value(), "Back from the first page returns nothing");
	check(!history.Back().has_value(), "Back stays empty once the history is exhausted");

	// The page we are on after Back is the current one: a later Visit stacks on top of it.
	PageHistory again;
	again.Visit(QStringLiteral("home"), 1);
	again.Visit(QStringLiteral("library"), 3);
	again.Back();
	again.Visit(QStringLiteral("settings"), 4);
	const std::optional<PageHistory::Entry> after = again.Back();
	check(after.has_value() && after->page == QStringLiteral("home") && after->focus == 1,
	      "Visit after Back stacks on top of the page Back returned to");

	// Revisiting the page already on top refreshes its focus instead of stacking a copy.
	PageHistory same;
	same.Visit(QStringLiteral("home"), 1);
	same.Visit(QStringLiteral("library"), 2);
	same.Visit(QStringLiteral("library"), 7);
	check(same.FocusFor(QStringLiteral("library")) == 7,
	      "revisiting the current page updates focus");
	const std::optional<PageHistory::Entry> before = same.Back();
	check(before.has_value() && before->page == QStringLiteral("home"),
	      "revisiting the current page does not stack a duplicate entry");
}

} // namespace

int main(int argc, char** argv) {
	QApplication application(argc, argv);
	int          failures = 0;
	const auto   check    = [&](bool value, const char* message) {
		if (!value) {
			std::fprintf(stderr, "FAILED: %s\n", message);
			++failures;
		}
	};

	TestTokenContrast(check);
	TestFontsAndStyleSheet(check);
	TestStyleSheetParses(check);
	TestFocusGrid(check);
	TestPageHistory(check);

	if (failures != 0) {
		std::fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}
	std::printf("launcher_console_ui: all checks passed\n");
	return 0;
}
