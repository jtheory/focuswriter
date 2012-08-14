/***********************************************************************
 *
 * Copyright (C) 2012 Graeme Gott <graeme@gottcode.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 ***********************************************************************/

#include "scene_list.h"

#include "action_manager.h"
#include "document.h"
#include "scene_model.h"

#include <QAction>
#include <QApplication>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMimeData>
#include <QMouseEvent>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QStyledItemDelegate>
#include <QTextBlock>
#include <QTextEdit>
#include <QToolButton>

//-----------------------------------------------------------------------------

namespace
{

class SceneDelegate : public QStyledItemDelegate
{
public:
	SceneDelegate(QObject* parent) :
		QStyledItemDelegate(parent)
	{
	}

	QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const;
};

QSize SceneDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
	QStyleOptionViewItemV4 opt = option;
	initStyleOption(&opt, index);
	const QWidget* widget = opt.widget;
	const QStyle* style = widget ? widget->style() : QApplication::style();

	QSize size = style->sizeFromContents(QStyle::CT_ItemViewItem, &opt, QSize(), widget);
	int margin = style->pixelMetric(QStyle::PM_FocusFrameVMargin, &opt, widget) * 2;
	int height = opt.fontMetrics.lineSpacing() * 3;
	size.setHeight(margin + height);
	return size;
}

}

//-----------------------------------------------------------------------------

SceneList::SceneList(QWidget* parent) :
	QFrame(parent),
	m_document(0),
	m_resizing(false)
{
	m_width = qBound(0, QSettings().value("SceneList/Width", qRound(3.5 * logicalDpiX())).toInt(), maximumWidth());

	// Configure sidebar
	setFrameStyle(QFrame::Panel | QFrame::Raised);
	setAutoFillBackground(true);
	setPalette(QApplication::palette());

	// Create actions for moving scenes
	QAction* action = new QAction(tr("Move Scenes Down"), this);
	action->setShortcut(tr("Ctrl+Shift+Down"));
	connect(action, SIGNAL(triggered()), this, SLOT(moveScenesDown()));
	addAction(action);
	ActionManager::instance()->addAction("MoveScenesDown", action);

	action = new QAction(tr("Move Scenes Up"), this);
	action->setShortcut(tr("Ctrl+Shift+Up"));
	connect(action, SIGNAL(triggered()), this, SLOT(moveScenesUp()));
	addAction(action);
	ActionManager::instance()->addAction("MoveScenesUp", action);

	// Create button to show scenes
	m_show_button = new QToolButton(this);
	m_show_button->setAutoRaise(true);
	m_show_button->setArrowType(Qt::RightArrow);
	m_show_button->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::MinimumExpanding);
	connect(m_show_button, SIGNAL(clicked()), this, SLOT(showScenes()));

	// Create button to hide scenes
	m_hide_button = new QToolButton(this);
	m_hide_button->setAutoRaise(true);
	m_hide_button->setArrowType(Qt::LeftArrow);
	m_hide_button->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::MinimumExpanding);
	connect(m_hide_button, SIGNAL(clicked()), this, SLOT(hideScenes()));

	// Create action for toggling scenes
	action = new QAction(tr("Toggle Scene List"), this);
	action->setShortcut(tr("Shift+F4"));
	ActionManager::instance()->addAction("ToggleScenes", action);
	connect(action, SIGNAL(changed()), this, SLOT(updateShortcuts()));
	updateShortcuts();

	// Create scene view
	m_filter_model = new QSortFilterProxyModel(this);
	m_filter_model->setFilterCaseSensitivity(Qt::CaseInsensitive);

	m_scenes = new QListView(this);
	m_scenes->setAlternatingRowColors(true);
	m_scenes->setDragEnabled(true);
	m_scenes->setDragDropMode(QAbstractItemView::InternalMove);
	m_scenes->setDropIndicatorShown(true);
	m_scenes->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_scenes->setItemDelegate(new SceneDelegate(m_scenes));
	m_scenes->setSelectionMode(QAbstractItemView::ExtendedSelection);
	m_scenes->setUniformItemSizes(true);
	m_scenes->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
	m_scenes->setWordWrap(true);
	m_scenes->viewport()->setAcceptDrops(true);
	m_scenes->setModel(m_filter_model);
	connect(m_scenes->selectionModel(), SIGNAL(currentChanged(QModelIndex,QModelIndex)), this, SLOT(sceneSelected(QModelIndex)));
	m_scenes->show();

	// Create filter widget
	m_filter = new QLineEdit(this);
#if (QT_VERSION >= QT_VERSION_CHECK(4,7,0))
	m_filter->setPlaceholderText(tr("Filter"));
#endif
	connect(m_filter, SIGNAL(textChanged(QString)), this, SLOT(setFilter(QString)));

	// Create widget for resizing
	m_resizer = new QFrame(this);
	m_resizer->setCursor(Qt::SizeHorCursor);
	m_resizer->setFrameStyle(QFrame::VLine | QFrame::Sunken);
	m_resizer->setToolTip(tr("Resize scene list"));

	// Lay out widgets
	QGridLayout* layout = new QGridLayout(this);
	layout->setMargin(layout->spacing());
	layout->setColumnStretch(1, 1);
	layout->setRowStretch(0, 1);
	layout->addWidget(m_show_button, 0, 0, 2, 1);
	layout->addWidget(m_hide_button, 0, 1, 2, 1);
	layout->addWidget(m_scenes, 0, 2);
	layout->addWidget(m_filter, 1, 2);
	layout->addWidget(m_resizer, 0, 3, 2, 1);

	// Start collapsed
	hideScenes();
}

//-----------------------------------------------------------------------------

SceneList::~SceneList()
{
	QSettings().setValue("SceneList/Width", m_width);
}

//-----------------------------------------------------------------------------

bool SceneList::scenesVisible() const
{
	return m_scenes->isVisible();
}

//-----------------------------------------------------------------------------

void SceneList::setDocument(Document* document)
{
	if (m_document) {
		disconnect(m_document->text(), SIGNAL(cursorPositionChanged()), this, SLOT(selectCurrentScene()));
	}
	m_document = 0;

	m_scenes->clearSelection();
	m_filter->clear();
	m_filter_model->setSourceModel(document->sceneModel());

	m_document = document;
	if (m_document && scenesVisible()) {
		m_document->sceneModel()->setUpdatesBlocked(false);
		connect(m_document->text(), SIGNAL(cursorPositionChanged()), this, SLOT(selectCurrentScene()));
		selectCurrentScene();
	}
}

//-----------------------------------------------------------------------------

void SceneList::hideScenes()
{
	if (m_document) {
		m_document->sceneModel()->setUpdatesBlocked(true);
		disconnect(m_document->text(), SIGNAL(cursorPositionChanged()), this, SLOT(selectCurrentScene()));
	}

	m_show_button->show();

	m_hide_button->hide();
	m_scenes->hide();
	m_filter->hide();
	m_resizer->hide();

	setMinimumWidth(0);
	setMaximumWidth(minimumSizeHint().width());

	m_filter->clear();

	if (m_document) {
		m_document->text()->setFocus();
	}

	if (!rect().contains(mapFromGlobal(QCursor::pos()))) {
		setMask(QRect(-1,-1,1,1));
	}
}

//-----------------------------------------------------------------------------

void SceneList::showScenes()
{
	clearMask();

	m_hide_button->show();
	m_scenes->show();
	m_filter->show();
	m_resizer->show();

	m_show_button->hide();

	setMinimumWidth(qRound(1.5 * logicalDpiX()));
	setMaximumWidth(m_width);

	if (m_document) {
		m_document->sceneModel()->setUpdatesBlocked(false);
		connect(m_document->text(), SIGNAL(cursorPositionChanged()), this, SLOT(selectCurrentScene()));
		selectCurrentScene();
	}

	m_scenes->setFocus();
}

//-----------------------------------------------------------------------------

void SceneList::mouseMoveEvent(QMouseEvent* event)
{
	if (m_resizing) {
		int delta = event->pos().x() - m_mouse_current.x();
		m_mouse_current = event->pos();

		m_width += delta;
		m_width = qMax(minimumWidth(), m_width);
		setMaximumWidth(m_width);

		event->accept();
	} else {
		QFrame::mouseMoveEvent(event);
	}
}

//-----------------------------------------------------------------------------

void SceneList::mousePressEvent(QMouseEvent* event)
{
	if (scenesVisible() &&
			(event->button() == Qt::LeftButton) &&
			(event->pos().x() >= m_resizer->mapToParent(m_resizer->rect().topLeft()).x())) {
		m_width = width();
		m_mouse_current = event->pos();
		m_resizing = true;

		event->accept();
	} else {
		QFrame::mousePressEvent(event);
	}
}

//-----------------------------------------------------------------------------

void SceneList::mouseReleaseEvent(QMouseEvent* event)
{
	if (event->button() == Qt::LeftButton) {
		m_resizing = false;
	}
	QFrame::mouseReleaseEvent(event);
}

//-----------------------------------------------------------------------------

void SceneList::resizeEvent(QResizeEvent* event)
{
	m_scenes->scrollTo(m_scenes->currentIndex());
	QFrame::resizeEvent(event);
}

//-----------------------------------------------------------------------------

void SceneList::moveScenesDown()
{
	moveSelectedScenes(1);
}

//-----------------------------------------------------------------------------

void SceneList::moveScenesUp()
{
	moveSelectedScenes(-1);
}

//-----------------------------------------------------------------------------

void SceneList::sceneSelected(const QModelIndex& index)
{
	if (!m_document || !scenesVisible()) {
		return;
	}

	if (index.isValid()) {
		int block_number = index.data(Qt::UserRole).toInt();
		QTextBlock block = m_document->text()->document()->findBlockByNumber(block_number);
		QTextCursor cursor = m_document->text()->textCursor();
		cursor.setPosition(block.position());
		m_document->text()->setTextCursor(cursor);
		m_document->centerCursor(true);
	}
}

//-----------------------------------------------------------------------------

void SceneList::selectCurrentScene()
{
	if (!m_document || !scenesVisible()) {
		return;
	}

	QModelIndex index = m_document->sceneModel()->findScene(m_document->text()->textCursor());
	if (index.isValid()) {
		index = m_filter_model->mapFromSource(index);
		m_scenes->selectionModel()->blockSignals(true);
		m_scenes->clearSelection();
		m_scenes->setCurrentIndex(index);
		m_scenes->scrollTo(index);
		m_scenes->selectionModel()->blockSignals(false);
	}
}

//-----------------------------------------------------------------------------

void SceneList::setFilter(const QString& filter)
{
	m_filter_model->setFilterFixedString(filter);
	if (filter.isEmpty()) {
		m_scenes->setDragEnabled(true);
		m_scenes->setSelectionMode(QAbstractItemView::ExtendedSelection);
	} else {
		m_scenes->setDragEnabled(false);
		m_scenes->setSelectionMode(QAbstractItemView::SingleSelection);
	}
}

//-----------------------------------------------------------------------------

void SceneList::updateShortcuts()
{
	QKeySequence shortcut = ActionManager::instance()->action("ToggleScenes")->shortcut();
	m_show_button->setShortcut(shortcut);
	m_show_button->setToolTip(tr("Show scene list (%1)").arg(shortcut.toString(QKeySequence::NativeText)));
	m_hide_button->setShortcut(shortcut);
	m_hide_button->setToolTip(tr("Hide scene list (%1)").arg(shortcut.toString(QKeySequence::NativeText)));
}

//-----------------------------------------------------------------------------

void SceneList::moveSelectedScenes(int movement)
{
	// Find scenes to move
	QModelIndexList indexes = m_filter_model->mapSelectionToSource(m_scenes->selectionModel()->selection()).indexes();
	if (indexes.isEmpty()) {
		return;
	}
	QList<int> scenes;

	// Find target row
	int first_row = INT_MAX;
	int last_row = 0;
	int index_row = 0;
	for (int i = 0, count = indexes.count(); i < count; ++i) {
		index_row = indexes.at(i).row();
		first_row = qMin(first_row, index_row);
		last_row = qMax(last_row, index_row);
		scenes.append(index_row);
	}
	int row = qMax(0, ((movement > 0) ? (last_row + 1) : first_row) + movement);

	// Move scenes
	m_document->sceneModel()->moveScenes(scenes, row);
}

//-----------------------------------------------------------------------------
