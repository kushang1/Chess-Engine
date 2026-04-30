#pragma once

#include <QtWidgets/QMainWindow>
#include "ui_MainWindow.h"

#include "Board.h"
#include "MoveGenerator.h"
#include "Engine.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QShortcut>
#include <QListWidget>
#include <QPainter>
#include <QPen>
#include <chrono>
#include <string>
#include <QtConcurrent>
#include <QFuture>
#include <QFutureWatcher>


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
    void highlightMoves(std::vector<Move>& moves);
    std::vector<QWidget*> highlightOverlays;
    void clearHighlights();
    bool eventFilter(QObject* obj, QEvent* event);

    void CalculateMoves();
    long long perft(int depth, board& b);

    void onHistoryItemSelected(int row);   // NEW
    // helpers
    QString moveToString(const Move& m, const board& before) const;
    QString squareToString(int sq) const;

    inline void truncateHistory(); 

    QString toSAN(const Move& m,  board& before, board& after);
    void addMoveToHistory(const Move& m, board& before, board& after);
    void highlightLastMove();
    bool checkDrawByRepetitionOr50();

private:

    Ui::mainwindowClass ui;
    QPushButton* boardButtons[8][8];  // 2D grid of buttons
    QLabel* turnLabel = NULL;
    QListWidget* moveHistoryList = nullptr;


    board gameBoard;
    MoveGenerator moveGenerator;
    int selectedSquare;
    bool pieceSelected = false;

    Engine engine;

    std::vector<board> positionHistory;  // position after each ply, [0] = initial
    std::vector<Move>  moveHistory;      // moves leading to those positions
    int currentMoveIndex = 0;            // index into positionHistory

    int lastFrom = -1;
    int lastTo = -1;

    
};
