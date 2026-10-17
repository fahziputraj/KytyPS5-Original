#include "consoleTheme.h"

#include <QFile>
#include <QFontDatabase>
#include <QLatin1Char>
#include <QLatin1String>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace {

constexpr auto kFontResource   = ":/fonts/DMSans.ttf";
constexpr auto kStyleResource  = ":/styles/console.qss";
constexpr auto kFallbackFamily = "Segoe UI";

// Family name the bundled font registered under. Empty until LoadFonts() succeeds.
QString g_font_family;

// One colour channel (0..1) to linear light, per WCAG 2.x.
double Linearize(double channel) {
	return channel <= 0.03928 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
}

double RelativeLuminance(const QColor& color) {
	const QColor rgb = color.toRgb();
	return 0.2126 * Linearize(rgb.redF()) + 0.7152 * Linearize(rgb.greenF()) +
	       0.0722 * Linearize(rgb.blueF());
}

} // namespace

namespace ConsoleTheme {

const Tokens& Get() {
	static const Tokens tokens = {
	    .bg             = QColor::fromRgb(0x0A1020),
	    .rail           = QColor::fromRgb(0x070C18),
	    .surface1       = QColor::fromRgb(0x0F1A33),
	    .surface2       = QColor::fromRgb(0x16223D),
	    .hero_base      = QColor::fromRgb(0x132A52),
	    .hero_accent    = QColor::fromRgb(0x1D3F78),
	    .accent         = QColor::fromRgb(0x3B82F6),
	    .text_primary   = QColor::fromRgb(0xE6ECF5),
	    .text_secondary = QColor::fromRgb(0xB7C6E3),
	    .text_muted     = QColor::fromRgb(0x7D8DB0),
	    .success        = QColor::fromRgb(0x86EFAC),
	    .warning        = QColor::fromRgb(0xFBBF24),
	    .danger         = QColor::fromRgb(0xF87171),
	    .focus          = QColor::fromRgb(0xFFFFFF),
	};
	return tokens;
}

double ContrastRatio(const QColor& foreground, const QColor& background) {
	const double a       = RelativeLuminance(foreground);
	const double b       = RelativeLuminance(background);
	const double lighter = std::max(a, b);
	const double darker  = std::min(a, b);
	return (lighter + 0.05) / (darker + 0.05);
}

QString FontFamily() {
	return g_font_family.isEmpty() ? QString::fromLatin1(kFallbackFamily) : g_font_family;
}

bool LoadFonts() {
	if (!g_font_family.isEmpty()) {
		return true;
	}
	const int id = QFontDatabase::addApplicationFont(QString::fromLatin1(kFontResource));
	if (id < 0) {
		return false;
	}
	const QStringList families = QFontDatabase::applicationFontFamilies(id);
	if (families.isEmpty()) {
		return false;
	}
	g_font_family = families.first();
	return true;
}

QString StyleSheet() {
	QFile style_sheet_file(QString::fromLatin1(kStyleResource));
	if (!style_sheet_file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		return {};
	}
	QString text = QString::fromUtf8(style_sheet_file.readAll());

	const Tokens& t       = Get();
	const auto    replace = [&text](const char* name, const QColor& color) {
		text.replace(QLatin1Char('@') + QLatin1String(name) + QLatin1Char('@'),
		             color.name(QColor::HexRgb).toUpper());
	};
	replace("bg", t.bg);
	replace("rail", t.rail);
	replace("surface1", t.surface1);
	replace("surface2", t.surface2);
	replace("hero_base", t.hero_base);
	replace("hero_accent", t.hero_accent);
	replace("accent", t.accent);
	replace("text_primary", t.text_primary);
	replace("text_secondary", t.text_secondary);
	replace("text_muted", t.text_muted);
	replace("success", t.success);
	replace("warning", t.warning);
	replace("danger", t.danger);
	replace("focus", t.focus);
	return text;
}

} // namespace ConsoleTheme
