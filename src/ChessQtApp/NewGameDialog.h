#pragma once

#include "GameSettings.h"

#include <QDialog>

class QButtonGroup;
class QComboBox;
class QLabel;
class QPushButton;

class NewGameDialog : public QDialog
{
    Q_OBJECT

public:
    explicit NewGameDialog(const GameSettings& settings, QWidget* parent = nullptr);

    GameSettings gameSettings() const;

private:
    QPushButton* createModeCard(const QString& title,
                                const QString& subtitle,
                                const QString& iconPath,
                                int id);
    QPushButton* createDifficultyCard(EngineDifficulty difficulty);
    void updateEngineControls();

    GameSettings m_initialSettings;
    QButtonGroup* m_modeGroup = nullptr;
    QButtonGroup* m_sideGroup = nullptr;
    QButtonGroup* m_difficultyGroup = nullptr;
    QComboBox* m_timeControlCombo = nullptr;
    QWidget* m_enginePanel = nullptr;
    QLabel* m_difficultyDetails = nullptr;
    QLabel* m_summaryLabel = nullptr;
};
