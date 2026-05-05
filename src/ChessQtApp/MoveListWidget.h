#pragma once

#include <QTableWidget>

class MoveListWidget : public QTableWidget
{
    Q_OBJECT

public:
    explicit MoveListWidget(QWidget* parent = nullptr);

    void clearMoves();
    void addMove(bool whiteMove, const QString& san);
    void setCurrentPly(int ply);
    void truncateToPly(int ply);

signals:
    void positionRequested(int positionIndex);
    void previousMoveRequested();
    void nextMoveRequested();

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    QTableWidgetItem* makeItem(const QString& text, Qt::Alignment alignment) const;
    void refreshRowHeights();
};
