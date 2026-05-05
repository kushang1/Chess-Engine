#include "MoveListWidget.h"

#include <QHeaderView>
#include <QKeyEvent>
#include <QScrollBar>

MoveListWidget::MoveListWidget(QWidget* parent)
    : QTableWidget(parent)
{
    setObjectName("MoveListWidget");
    setColumnCount(3);
    setHorizontalHeaderLabels({ "#", "White", "Black" });
    verticalHeader()->hide();
    horizontalHeader()->setHighlightSections(false);
    horizontalHeader()->setSectionsClickable(false);
    horizontalHeader()->setStretchLastSection(false);
    horizontalHeader()->resizeSection(0, 42);
    horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    setShowGrid(false);
    setAlternatingRowColors(true);
    setSelectionBehavior(QAbstractItemView::SelectItems);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setFocusPolicy(Qt::StrongFocus);
    setWordWrap(false);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    verticalScrollBar()->setSingleStep(28);

    connect(this, &QTableWidget::cellClicked, this, [this](int row, int column) {
        if (row < 0 || column < 1) {
            return;
        }
        const int positionIndex = row * 2 + column;
        emit positionRequested(positionIndex);
    });
}

void MoveListWidget::clearMoves()
{
    setRowCount(0);
}

void MoveListWidget::addMove(bool whiteMove, const QString& san)
{
    if (whiteMove || rowCount() == 0) {
        const int row = rowCount();
        insertRow(row);
        setItem(row, 0, makeItem(QString::number(row + 1) + ".", Qt::AlignCenter));
        setItem(row, 1, makeItem(san, Qt::AlignVCenter | Qt::AlignLeft));
        setItem(row, 2, makeItem(QString(), Qt::AlignVCenter | Qt::AlignLeft));
    }
    else {
        const int row = rowCount() - 1;
        if (!item(row, 2)) {
            setItem(row, 2, makeItem(QString(), Qt::AlignVCenter | Qt::AlignLeft));
        }
        item(row, 2)->setText(san);
    }

    refreshRowHeights();
    scrollToBottom();
}

void MoveListWidget::setCurrentPly(int ply)
{
    clearSelection();
    if (ply <= 0) {
        setCurrentCell(-1, -1);
        return;
    }

    const int row = (ply - 1) / 2;
    const int column = (ply % 2 == 1) ? 1 : 2;
    if (row >= rowCount()) {
        return;
    }

    setCurrentCell(row, column, QItemSelectionModel::ClearAndSelect);
    scrollToItem(item(row, column), QAbstractItemView::PositionAtCenter);
}

void MoveListWidget::truncateToPly(int ply)
{
    const int rowsToKeep = (ply + 1) / 2;
    while (rowCount() > rowsToKeep) {
        removeRow(rowCount() - 1);
    }

    if (ply > 0 && (ply % 2) == 1 && rowsToKeep > 0 && item(rowsToKeep - 1, 2)) {
        item(rowsToKeep - 1, 2)->setText(QString());
    }
}

void MoveListWidget::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Left) {
        emit previousMoveRequested();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Right) {
        emit nextMoveRequested();
        event->accept();
        return;
    }
    QTableWidget::keyPressEvent(event);
}

QTableWidgetItem* MoveListWidget::makeItem(const QString& text, Qt::Alignment alignment) const
{
    auto* tableItem = new QTableWidgetItem(text);
    tableItem->setTextAlignment(alignment);
    tableItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
    return tableItem;
}

void MoveListWidget::refreshRowHeights()
{
    for (int row = 0; row < rowCount(); ++row) {
        setRowHeight(row, 30);
    }
}
