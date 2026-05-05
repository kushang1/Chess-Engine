#pragma once

#include "ChessBoardWidget.h"

#include <QString>
#include <QtGlobal>

enum class GameMode {
    HumanVsHuman,
    HumanVsEngine
};

enum class EngineDifficulty {
    Beginner,
    Easy,
    Medium,
    Hard,
    Expert
};

enum class PlayerSide {
    White,
    Black,
    Random
};

struct UiPreferences {
    bool darkTheme = true;
    ChessBoardWidget::BoardTheme boardTheme = ChessBoardWidget::BoardTheme::Modern;
    bool coordinatesVisible = true;
    bool soundsEnabled = true;
    bool animationsEnabled = true;
    bool legalMoveHints = true;
    bool autoQueenPromotion = true;
    bool confirmResign = true;
    bool showAnalysis = true;
    bool showThinkingIndicator = true;
    QString pieceStyle = "Classic";
    EngineDifficulty defaultEngineDifficulty = EngineDifficulty::Medium;
};

struct GameSettings {
    GameMode gameMode = GameMode::HumanVsEngine;
    PlayerSide playerSide = PlayerSide::White;
    EngineDifficulty engineDifficulty = EngineDifficulty::Medium;
    qint64 initialTimeMs = 5 * 60 * 1000;
};

struct AppSettings {
    UiPreferences ui;
    GameSettings game;

    static AppSettings load();
    void save() const;
};

QString gameModeText(GameMode mode);
QString playerSideText(PlayerSide side);
QString difficultyText(EngineDifficulty difficulty);
QString boardThemeText(ChessBoardWidget::BoardTheme theme);
int difficultyDepth(EngineDifficulty difficulty);
int difficultyMoveTimeMs(EngineDifficulty difficulty);
