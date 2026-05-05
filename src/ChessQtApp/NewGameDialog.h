#pragma once

#include "GameSettings.h"

#include <QDialog>

class QButtonGroup;
class QComboBox;
class QLabel;
class QWidget;

class NewGameDialog : public QDialog
{
    Q_OBJECT

public:
    explicit NewGameDialog(const GameSettings& settings, QWidget* parent = nullptr);

    GameSettings gameSettings() const;

private:
    QWidget* createModeCard(const QString& title, const QString& subtitle, int id);
    void updateEngineControls();

    GameSettings m_initialSettings;
    QButtonGroup* m_modeGroup = nullptr;
    QButtonGroup* m_sideGroup = nullptr;
    QComboBox* m_difficultyCombo = nullptr;
    QComboBox* m_timeControlCombo = nullptr;
    QWidget* m_enginePanel = nullptr;
    QLabel* m_summaryLabel = nullptr;
};
