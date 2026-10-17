#ifndef CONSOLE_THEME_H
#define CONSOLE_THEME_H

#include <QColor>
#include <QString>

// Visual tokens, bundled font and shared style sheet of the console-style launcher.
// Pages read colours from Get() and never carry raw hex values of their own.
namespace ConsoleTheme {

struct Tokens {
	QColor bg;
	QColor rail;
	QColor surface1;
	QColor surface2;
	QColor hero_base;
	QColor hero_accent;
	QColor accent;
	QColor text_primary;
	QColor text_secondary;
	QColor text_muted;
	QColor success;
	QColor warning;
	QColor danger;
	QColor focus;
};

const Tokens& Get();

// WCAG 2.x contrast ratio (1.0 to 21.0) of two colours. The lighter one is always the
// numerator, so the order of the arguments does not matter.
double ContrastRatio(const QColor& foreground, const QColor& background);

// "DM Sans" once LoadFonts() has registered the bundled font, "Segoe UI" otherwise.
QString FontFamily();

// Registers the bundled DM Sans (:/fonts/DMSans.ttf). Returns false when that fails, and
// FontFamily() then stays on "Segoe UI". Needs the QGuiApplication; repeat calls are harmless.
bool LoadFonts();

// :/styles/console.qss with every @token@ placeholder replaced by the token's #RRGGBB value.
// Empty when the resource cannot be read.
QString StyleSheet();

} // namespace ConsoleTheme

#endif // CONSOLE_THEME_H
