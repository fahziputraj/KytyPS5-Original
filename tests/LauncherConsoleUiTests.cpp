#include "consoleTheme.h"

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

	if (failures != 0) {
		std::fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}
	std::printf("launcher_console_ui: all checks passed\n");
	return 0;
}
