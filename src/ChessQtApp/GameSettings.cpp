#include "GameSettings.h"

#include <QSettings>
#include <QThread>

#include <algorithm>

namespace {

constexpr int kSettingsVersion = 1;

template <typename Enum>
Enum enumFromInt(int value, Enum fallback, int maxInclusive)
{
    if (value < 0 || value > maxInclusive) {
        return fallback;
    }
    return static_cast<Enum>(value);
}

} // namespace

AppSettings AppSettings::load()
{
    QSettings settings("CodexChess", "ChessQtApp");
    AppSettings appSettings;

    settings.beginGroup("Application");
    const int version = settings.value("version", kSettingsVersion).toInt();
    Q_UNUSED(version);
    settings.endGroup();

    settings.beginGroup("Appearance");
    appSettings.ui.darkTheme = settings.value("darkTheme", appSettings.ui.darkTheme).toBool();
    appSettings.ui.boardTheme = enumFromInt(settings.value("boardTheme", static_cast<int>(appSettings.ui.boardTheme)).toInt(),
                                            appSettings.ui.boardTheme,
                                            static_cast<int>(ChessBoardWidget::BoardTheme::Wood));
    appSettings.ui.coordinatesVisible = settings.value("coordinatesVisible", appSettings.ui.coordinatesVisible).toBool();
    appSettings.ui.animationsEnabled = settings.value("animationsEnabled", appSettings.ui.animationsEnabled).toBool();
    appSettings.ui.pieceStyle = settings.value("pieceStyle", appSettings.ui.pieceStyle).toString();
    settings.endGroup();

    settings.beginGroup("Gameplay");
    appSettings.ui.soundsEnabled = settings.value("soundsEnabled", appSettings.ui.soundsEnabled).toBool();
    appSettings.ui.legalMoveHints = settings.value("legalMoveHints", appSettings.ui.legalMoveHints).toBool();
    appSettings.ui.autoQueenPromotion = settings.value("autoQueenPromotion", appSettings.ui.autoQueenPromotion).toBool();
    appSettings.ui.confirmResign = settings.value("confirmResign", appSettings.ui.confirmResign).toBool();
    settings.endGroup();

    settings.beginGroup("Engine");
    appSettings.ui.defaultEngineDifficulty = enumFromInt(settings.value("defaultDifficulty", static_cast<int>(appSettings.ui.defaultEngineDifficulty)).toInt(),
                                                         appSettings.ui.defaultEngineDifficulty,
                                                         static_cast<int>(EngineDifficulty::Master));
    appSettings.ui.showAnalysis = settings.value("showAnalysis", appSettings.ui.showAnalysis).toBool();
    appSettings.ui.showThinkingIndicator = settings.value("showThinkingIndicator", appSettings.ui.showThinkingIndicator).toBool();
    settings.endGroup();

    settings.beginGroup("NewGame");
    appSettings.game.gameMode = enumFromInt(settings.value("mode", static_cast<int>(appSettings.game.gameMode)).toInt(),
                                            appSettings.game.gameMode,
                                            static_cast<int>(GameMode::HumanVsEngine));
    appSettings.game.playerSide = enumFromInt(settings.value("side", static_cast<int>(appSettings.game.playerSide)).toInt(),
                                              appSettings.game.playerSide,
                                              static_cast<int>(PlayerSide::Random));
    appSettings.game.engineDifficulty = enumFromInt(settings.value("difficulty", static_cast<int>(appSettings.ui.defaultEngineDifficulty)).toInt(),
                                                    appSettings.ui.defaultEngineDifficulty,
                                                    static_cast<int>(EngineDifficulty::Master));
    appSettings.game.initialTimeMs = settings.value("initialTimeMs", appSettings.game.initialTimeMs).toLongLong();
    settings.endGroup();

    return appSettings;
}

void AppSettings::save() const
{
    QSettings settings("CodexChess", "ChessQtApp");

    settings.beginGroup("Application");
    settings.setValue("version", kSettingsVersion);
    settings.endGroup();

    settings.beginGroup("Appearance");
    settings.setValue("darkTheme", ui.darkTheme);
    settings.setValue("boardTheme", static_cast<int>(ui.boardTheme));
    settings.setValue("coordinatesVisible", ui.coordinatesVisible);
    settings.setValue("animationsEnabled", ui.animationsEnabled);
    settings.setValue("pieceStyle", ui.pieceStyle);
    settings.endGroup();

    settings.beginGroup("Gameplay");
    settings.setValue("soundsEnabled", ui.soundsEnabled);
    settings.setValue("legalMoveHints", ui.legalMoveHints);
    settings.setValue("autoQueenPromotion", ui.autoQueenPromotion);
    settings.setValue("confirmResign", ui.confirmResign);
    settings.endGroup();

    settings.beginGroup("Engine");
    settings.setValue("defaultDifficulty", static_cast<int>(ui.defaultEngineDifficulty));
    settings.setValue("showAnalysis", ui.showAnalysis);
    settings.setValue("showThinkingIndicator", ui.showThinkingIndicator);
    settings.endGroup();

    settings.beginGroup("NewGame");
    settings.setValue("mode", static_cast<int>(game.gameMode));
    settings.setValue("side", static_cast<int>(game.playerSide));
    settings.setValue("difficulty", static_cast<int>(game.engineDifficulty));
    settings.setValue("initialTimeMs", game.initialTimeMs);
    settings.endGroup();
}

QString gameModeText(GameMode mode)
{
    switch (mode) {
    case GameMode::HumanVsHuman: return "Human vs Human";
    case GameMode::HumanVsEngine: return "Human vs Engine";
    }
    return "Human vs Engine";
}

QString playerSideText(PlayerSide side)
{
    switch (side) {
    case PlayerSide::White: return "White";
    case PlayerSide::Black: return "Black";
    case PlayerSide::Random: return "Random";
    }
    return "White";
}

QString difficultyText(EngineDifficulty difficulty)
{
    switch (difficulty) {
    case EngineDifficulty::Beginner: return "Beginner";
    case EngineDifficulty::Intermediate: return "Intermediate";
    case EngineDifficulty::Advanced: return "Advanced";
    case EngineDifficulty::Expert: return "Expert";
    case EngineDifficulty::Master: return "Master";
    }
    return "Advanced";
}

QString difficultyDescription(EngineDifficulty difficulty)
{
    switch (difficulty) {
    case EngineDifficulty::Beginner: return "Learns with you and responds almost instantly.";
    case EngineDifficulty::Intermediate: return "Sees short tactics and punishes loose pieces.";
    case EngineDifficulty::Advanced: return "A balanced club-level challenge with deeper plans.";
    case EngineDifficulty::Expert: return "Calculates hard, uses multiple threads, and rarely slips.";
    case EngineDifficulty::Master: return "Maximum engine depth, time, memory, and available CPU power.";
    }
    return {};
}

QString difficultySpecText(EngineDifficulty difficulty)
{
    const int depth = difficultyDepth(difficulty);
    const int milliseconds = difficultyMoveTimeMs(difficulty);
    const QString time = milliseconds >= 1000
        ? QString("%1s").arg(milliseconds / 1000.0, 0, 'g', 3)
        : QString("%1ms").arg(milliseconds);
    return QString("Depth %1  |  %2  |  %3 thread%4  |  %5 MB hash")
        .arg(depth)
        .arg(time)
        .arg(difficultyThreadCount(difficulty))
        .arg(difficultyThreadCount(difficulty) == 1 ? "" : "s")
        .arg(difficultyHashSizeMb(difficulty));
}

QString boardThemeText(ChessBoardWidget::BoardTheme theme)
{
    switch (theme) {
    case ChessBoardWidget::BoardTheme::Classic: return "Classic";
    case ChessBoardWidget::BoardTheme::Modern: return "Modern";
    case ChessBoardWidget::BoardTheme::Blue: return "Blue";
    case ChessBoardWidget::BoardTheme::Green: return "Green";
    case ChessBoardWidget::BoardTheme::Wood: return "Wood";
    }
    return "Modern";
}

int difficultyDepth(EngineDifficulty difficulty)
{
    switch (difficulty) {
    case EngineDifficulty::Beginner: return 2;
    case EngineDifficulty::Intermediate: return 5;
    case EngineDifficulty::Advanced: return 8;
    case EngineDifficulty::Expert: return 14;
    case EngineDifficulty::Master: return 128;
    }
    return 8;
}

int difficultyMoveTimeMs(EngineDifficulty difficulty)
{
    switch (difficulty) {
    case EngineDifficulty::Beginner: return 180;
    case EngineDifficulty::Intermediate: return 700;
    case EngineDifficulty::Advanced: return 2000;
    case EngineDifficulty::Expert: return 6000;
    case EngineDifficulty::Master: return 15000;
    }
    return 2000;
}

int difficultyThreadCount(EngineDifficulty difficulty)
{
    const int available = std::max(1, QThread::idealThreadCount());
    switch (difficulty) {
    case EngineDifficulty::Beginner: return 1;
    case EngineDifficulty::Intermediate: return 1;
    case EngineDifficulty::Advanced: return std::min(2, available);
    case EngineDifficulty::Expert: return std::min(4, available);
    case EngineDifficulty::Master: return std::min(32, available);
    }
    return 1;
}

int difficultyHashSizeMb(EngineDifficulty difficulty)
{
    switch (difficulty) {
    case EngineDifficulty::Beginner: return 16;
    case EngineDifficulty::Intermediate: return 32;
    case EngineDifficulty::Advanced: return 96;
    case EngineDifficulty::Expert: return 256;
    case EngineDifficulty::Master: return 512;
    }
    return 96;
}
