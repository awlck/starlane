#include "debuglogwindow.h"

#include <QtCore/QSettings>
#include <QtGui/QFontDatabase>
#include <QtGui/QPalette>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollBar>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

namespace {
constexpr auto kEnabledCategoriesSettingsKey = "debugLogEnabledCategories";
// A long session logging at, say, every event tick can otherwise grow the document without bound;
// QPlainTextEdit::setMaximumBlockCount below discards from the top once this is exceeded, on its
// own, cheaper than trimming by hand every time a new line comes in.
constexpr int kMaxLines = 20000;
}  // namespace

DebugLogWindow::DebugLogWindow(QWidget *parent) : QDockWidget(tr("Debug Log"), parent) {
	setObjectName(QStringLiteral("debugLogWindow"));
	setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable
		| QDockWidget::DockWidgetFloatable);
	setAllowedAreas(Qt::AllDockWidgetAreas);

	auto *container = new QWidget(this);
	auto *layout = new QVBoxLayout(container);

	// Which categories were checked last time, so a player/author who has, say, narrowed this down
	// to just "Restrictions" doesn't have to redo that every time they reopen the panel. Absent (the
	// very first run) defaults to "every category", matching starlane-console's unconditional dump.
	const uint32_t savedMask = (uint32_t) QSettings().value(kEnabledCategoriesSettingsKey,
		(uint32_t) -1).toUInt();

	auto *categoryBox = new QGroupBox(tr("Categories"), container);
	auto *categoryLayout = new QGridLayout(categoryBox);
	constexpr int kColumns = 4;
	for (uint32_t i = 0; i < Starlane::kDebugCategoryCount; i++) {
		auto category = (Starlane::DebugCategory) i;
		auto *check = new QCheckBox(QString::fromUtf8(Starlane::DebugCategoryName(category)), categoryBox);
		check->setChecked((savedMask & (1u << i)) != 0);
		connect(check, &QCheckBox::toggled, this, [this] { ApplyEnabledCategories(); });
		categoryLayout->addWidget(check, (int) i / kColumns, (int) i % kColumns);
		categoryChecks[i] = check;
	}
	layout->addWidget(categoryBox);

	auto *toolbar = new QHBoxLayout;
	auto *allButton = new QPushButton(tr("All"), container);
	connect(allButton, &QPushButton::clicked, this, [this] {
		for (auto *check : categoryChecks) check->setChecked(true);
	});
	auto *noneButton = new QPushButton(tr("None"), container);
	connect(noneButton, &QPushButton::clicked, this, [this] {
		for (auto *check : categoryChecks) check->setChecked(false);
	});
	auto *clearButton = new QPushButton(tr("Clear Log"), container);
	connect(clearButton, &QPushButton::clicked, this, [this] { log->clear(); });
	toolbar->addWidget(allButton);
	toolbar->addWidget(noneButton);
	toolbar->addStretch(1);
	toolbar->addWidget(clearButton);
	layout->addLayout(toolbar);

	log = new QPlainTextEdit(container);
	log->setReadOnly(true);
	log->setLineWrapMode(QPlainTextEdit::NoWrap);
	log->setMaximumBlockCount(kMaxLines);
	log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
	// True black, matching the game output panes (see MainWindow) -- this is otherwise the one
	// widget in the app that would default to the theme's ordinary (dark gray) base color.
	QPalette palette = log->palette();
	palette.setColor(QPalette::Base, Qt::black);
	log->setPalette(palette);
	layout->addWidget(log, 1);

	setWidget(container);

	ApplyEnabledCategories();
}

void DebugLogWindow::ApplyEnabledCategories() {
	uint32_t mask = 0;
	for (uint32_t i = 0; i < Starlane::kDebugCategoryCount; i++) {
		const bool checked = categoryChecks[i]->isChecked();
		if (checked) mask |= 1u << i;
		// Gated on isVisible() too, not just the checkbox: while this panel is hidden (or merely
		// tabbed behind another dock), there is nobody to show these events to, so starlane-core
		// shouldn't pay to format them at all -- see debuglog.h's SL_DEBUG macro. Called
		// unconditionally (not skipped for an unchecked box) since unchecking one has to actually
		// turn it off in core, not just leave whatever was set the last time it was checked.
		Starlane::SetDebugEventCategoryEnabled((Starlane::DebugCategory) i, checked && isVisible());
	}
	QSettings().setValue(kEnabledCategoriesSettingsKey, mask);
}

void DebugLogWindow::showEvent(QShowEvent *event) {
	QDockWidget::showEvent(event);
	ApplyEnabledCategories();
}

void DebugLogWindow::hideEvent(QHideEvent *event) {
	QDockWidget::hideEvent(event);
	// Every category off, regardless of which boxes are checked: nothing should keep costing
	// starlane-core anything once there's no visible panel for it to show up in.
	for (uint32_t i = 0; i < Starlane::kDebugCategoryCount; i++)
		Starlane::SetDebugEventCategoryEnabled((Starlane::DebugCategory) i, false);
}

void DebugLogWindow::AppendEvent(Starlane::DebugCategory category, const QString &message) {
	// Scroll-position-preserving append, the same idea as OutputFormatter::EndBatch(): only follow
	// new output down if the player was already at the bottom, so reading back through the log isn't
	// constantly yanked away from underneath them by a game that's still busy logging.
	QScrollBar *scrollbar = log->verticalScrollBar();
	const bool wasAtBottom = scrollbar->value() >= scrollbar->maximum() - 2;

	log->appendPlainText(QStringLiteral("[%1] %2")
		.arg(QString::fromUtf8(Starlane::DebugCategoryName(category)), message));

	if (wasAtBottom) scrollbar->setValue(scrollbar->maximum());
}
