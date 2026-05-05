#pragma once

#include "GameSettings.h"

#include <QDialog>

class QCheckBox;
class QComboBox;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(const UiPreferences& preferences, QWidget* parent = nullptr);

    UiPreferences preferences() const;

private:
    QComboBox* m_themeCombo = nullptr;
    QComboBox* m_boardCombo = nullptr;
    QComboBox* m_pieceCombo = nullptr;
    QComboBox* m_defaultDifficultyCombo = nullptr;
    QCheckBox* m_coordinatesCheck = nullptr;
    QCheckBox* m_soundsCheck = nullptr;
    QCheckBox* m_animationsCheck = nullptr;
    QCheckBox* m_legalMoveHintsCheck = nullptr;
    QCheckBox* m_autoQueenCheck = nullptr;
    QCheckBox* m_confirmResignCheck = nullptr;
    QCheckBox* m_showAnalysisCheck = nullptr;
    QCheckBox* m_showThinkingCheck = nullptr;
};
