#pragma once

#include "ChessBoardWidget.h"
#include "EngineController.h"
#include "GameSettings.h"
#include "SettingsDialog.h"
#include "SidebarWidget.h"

#include <ChessEngine/EngineFacade.h>

#include <QElapsedTimer>
#include <QMainWindow>
#include <QSoundEffect>
#include <QTimer>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

class QAction;
class QDockWidget;
class QLabel;
class QStackedWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private:
    void buildInterface();
    void buildToolbar();
    void buildSidebarDock();
    void buildBottomDock();
    void applyTheme();
    void animatePanel(QWidget* widget);
    void setupSounds();
    void writeToneFile(const QString& path, int frequency, int durationMs);

    void newGame();
    void startGame(const GameSettings& settings);
    void undoMove();
    void redoMove();
    void resignGame();
    void openSettings();
    void flipBoard();
    void toggleAnalysisDock();
    void updateBoardInputState();
    void updateActionStates();
    bool isBoardInputAllowed() const;
    bool isEngineTurn() const;
    bool isHumanSideToMove() const;
    bool isLatestPosition() const;
    void maybeStartEngineTurn();
    void handleEngineSearchStarted(int generation);
    void handleEngineSearchFinished(int generation,
                                    Move bestMove,
                                    long long nodes,
                                    long long leafNodes,
                                    int depth,
                                    int moveTimeMs);

    void handleSquareClicked(int square);
    void handleDragStarted(int square);
    void handleMoveRequested(int from, int to);
    void handleHistoryPositionRequested(int positionIndex);
    void selectSquare(int square);
    void clearSelection();
    std::vector<Move> legalMovesFrom(int square) const;
    bool tryMakeMove(int from, int to);
    void completeMove(const Move& move, bool whiteMove, const QString& san, bool animate);

    std::array<Piece, 64> boardSnapshot() const;
    void refreshBoard(bool animate = false, int from = -1, int to = -1);
    void updateGameStatusViews();
    bool checkDrawByRepetitionOr50();
    bool updateGameStatusLabel();
    void updateModeStatus();
    QString modeStatusText() const;
    QString moveText(const Move& move) const;
    int checkedKingSquare() const;
    int materialForWhite(bool white) const;
    void playMoveFeedback(const Move& move);
    void updateClocks();
    void resetClocks();

    void recordCurrentPosition();
    void restorePosition(int positionIndex);
    void truncateHistory();
    void addMoveToHistory(bool whiteMove, const QString& san);

    chess::ChessEngine m_engine;
    EngineController* m_engineController = nullptr;
    ChessBoardWidget* m_board = nullptr;
    SidebarWidget* m_sidebar = nullptr;
    QStackedWidget* m_stack = nullptr;
    QDockWidget* m_sidebarDock = nullptr;
    QDockWidget* m_bottomDock = nullptr;
    QLabel* m_centerStatus = nullptr;

    QAction* m_newGameAction = nullptr;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    QAction* m_resignAction = nullptr;
    QAction* m_flipAction = nullptr;
    QAction* m_analysisAction = nullptr;
    QAction* m_settingsAction = nullptr;

    QTimer m_clockTimer;
    QElapsedTimer m_clockElapsed;
    qint64 m_whiteRemainingMs = 5 * 60 * 1000;
    qint64 m_blackRemainingMs = 5 * 60 * 1000;
    bool m_unlimitedTime = false;

    QSoundEffect m_moveSound;
    QSoundEffect m_captureSound;
    QSoundEffect m_checkSound;

    AppSettings m_appSettings;
    PlayerSide m_resolvedHumanSide = PlayerSide::White;
    int m_selectedSquare = -1;
    bool m_engineThinking = false;
    bool m_moveAnimationInProgress = false;
    bool m_gameFinished = false;
    bool m_activeClockWhite = true;
    int m_gameGeneration = 0;
    int m_lastSearchDepth = 0;
    int m_lastSearchMoveTimeMs = 0;

    std::vector<std::string> m_positionHistory;
    std::vector<uint64_t> m_repetitionHistory;
    std::vector<Move> m_moveHistory;
    int m_currentMoveIndex = 0;
    int m_lastFrom = -1;
    int m_lastTo = -1;
};
