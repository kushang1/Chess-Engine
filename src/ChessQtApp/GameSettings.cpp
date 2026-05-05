#include "GameSettings.h"

#include <QSettings>

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
                                                         static_cast<int>(EngineDifficulty::Expert));
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
                                                    static_cast<int>(EngineDifficulty::Expert));
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
    case EngineDifficulty::Easy: return "Easy";
    case EngineDifficulty::Medium: return "Medium";
    case EngineDifficulty::Hard: return "Hard";
    case EngineDifficulty::Expert: return "Expert";
    }
    return "Medium";
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
    case EngineDifficulty::Beginner: return 1;
    case EngineDifficulty::Easy: return 2;
    case EngineDifficulty::Medium: return 4;
    case EngineDifficulty::Hard: return 6;
    case EngineDifficulty::Expert: return 8;
    }
    return 4;
}

int difficultyMoveTimeMs(EngineDifficulty difficulty)
{
    switch (difficulty) {
    case EngineDifficulty::Beginner: return 120;
    case EngineDifficulty::Easy: return 250;
    case EngineDifficulty::Medium: return 600;
    case EngineDifficulty::Hard: return 1100;
    case EngineDifficulty::Expert: return 1800;
    }
    return 600;
}
