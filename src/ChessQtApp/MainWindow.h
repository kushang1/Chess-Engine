#pragma once

#include <ChessEngine/EngineFacade.h>

#include "ui_MainWindow.h"

#include <QFuture>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QShortcut>
#include <QtConcurrent>
#include <QtWidgets/QMainWindow>
#include <QVBoxLayout>

#include <cstdint>
#include <string>
#include <vector>

QT_BEGIN_NAMESPACE
namespace Ui {
    class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private:
    void handleTileClick();
    void updateBoardUI();
    void resetColors();
    void undoMove();
    void redoMove();
    void highlightMoves(const std::vector<Move>& moves);
    void clearHighlights();
    bool eventFilter(QObject* obj, QEvent* event) override;

    void CalculateMoves();
    void onHistoryItemSelected(int row);

    void recordCurrentPosition();
    void restorePosition(int positionIndex);
    void truncateHistory();
    void addMoveToHistory(bool whiteMove, const QString& san);
    void highlightLastMove();
    bool checkDrawByRepetitionOr50();
    bool updateGameStatusLabel();

    Ui::mainwindowClass ui;
    QPushButton* boardButtons[8][8]{};
    QLabel* turnLabel = nullptr;
    QListWidget* moveHistoryList = nullptr;
    std::vector<QWidget*> highlightOverlays;

    chess::ChessEngine engine;
    int selectedSquare = -1;
    bool pieceSelected = false;

    std::vector<std::string> positionHistory;
    std::vector<uint64_t> repetitionHistory;
    std::vector<Move> moveHistory;
    int currentMoveIndex = 0;

    int lastFrom = -1;
    int lastTo = -1;
};
