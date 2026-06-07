#include "SettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QIcon>
#include <QLabel>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {

QWidget* makeTab(QWidget* parent)
{
    auto* tab = new QWidget(parent);
    auto* layout = new QFormLayout(tab);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setHorizontalSpacing(18);
    layout->setVerticalSpacing(12);
    return tab;
}

QFormLayout* formOf(QWidget* widget)
{
    return qobject_cast<QFormLayout*>(widget->layout());
}

} // namespace

SettingsDialog::SettingsDialog(const UiPreferences& preferences, QWidget* parent)
    : QDialog(parent)
{
    setObjectName("SettingsDialog");
    setWindowTitle("Settings");
    setModal(true);
    resize(460, 410);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(22, 22, 22, 22);
    root->setSpacing(16);

    auto* title = new QLabel("Settings", this);
    title->setObjectName("DialogTitle");
    root->addWidget(title);

    auto* subtitle = new QLabel("Shape the board, feedback, and engine experience.", this);
    subtitle->setObjectName("MutedLabel");
    root->addWidget(subtitle);

    auto* tabs = new QTabWidget(this);
    tabs->setObjectName("SettingsTabs");

    QWidget* appearance = makeTab(tabs);
    m_themeCombo = new QComboBox(appearance);
    m_themeCombo->addItems({ "Dark", "Light" });
    m_themeCombo->setCurrentIndex(preferences.darkTheme ? 0 : 1);
    formOf(appearance)->addRow("App theme", m_themeCombo);

    m_boardCombo = new QComboBox(appearance);
    for (int i = 0; i <= static_cast<int>(ChessBoardWidget::BoardTheme::Wood); ++i) {
        m_boardCombo->addItem(boardThemeText(static_cast<ChessBoardWidget::BoardTheme>(i)));
    }
    m_boardCombo->setCurrentIndex(static_cast<int>(preferences.boardTheme));
    formOf(appearance)->addRow("Board preset", m_boardCombo);

    m_pieceCombo = new QComboBox(appearance);
    m_pieceCombo->addItems({ "Classic" });
    m_pieceCombo->setCurrentText(preferences.pieceStyle);
    formOf(appearance)->addRow("Pieces", m_pieceCombo);

    m_coordinatesCheck = new QCheckBox("Show board coordinates", appearance);
    m_coordinatesCheck->setChecked(preferences.coordinatesVisible);
    formOf(appearance)->addRow(QString(), m_coordinatesCheck);

    m_animationsCheck = new QCheckBox("Smooth move animations", appearance);
    m_animationsCheck->setChecked(preferences.animationsEnabled);
    formOf(appearance)->addRow(QString(), m_animationsCheck);
    tabs->addTab(appearance, QIcon(":/icons/theme.svg"), "Appearance");

    QWidget* gameplay = makeTab(tabs);
    m_legalMoveHintsCheck = new QCheckBox("Show legal move hints", gameplay);
    m_legalMoveHintsCheck->setChecked(preferences.legalMoveHints);
    formOf(gameplay)->addRow(QString(), m_legalMoveHintsCheck);

    m_autoQueenCheck = new QCheckBox("Auto-queen promotions", gameplay);
    m_autoQueenCheck->setChecked(preferences.autoQueenPromotion);
    formOf(gameplay)->addRow(QString(), m_autoQueenCheck);

    m_confirmResignCheck = new QCheckBox("Confirm resign", gameplay);
    m_confirmResignCheck->setChecked(preferences.confirmResign);
    formOf(gameplay)->addRow(QString(), m_confirmResignCheck);

    m_soundsCheck = new QCheckBox("Sound effects", gameplay);
    m_soundsCheck->setChecked(preferences.soundsEnabled);
    formOf(gameplay)->addRow(QString(), m_soundsCheck);
    tabs->addTab(gameplay, QIcon(":/icons/sound.svg"), "Gameplay");

    QWidget* engine = makeTab(tabs);
    m_defaultDifficultyCombo = new QComboBox(engine);
    for (int i = 0; i <= static_cast<int>(EngineDifficulty::Master); ++i) {
        const auto difficulty = static_cast<EngineDifficulty>(i);
        m_defaultDifficultyCombo->addItem(difficultyText(difficulty));
        m_defaultDifficultyCombo->setItemData(i,
                                              difficultyDescription(difficulty) + "\n" + difficultySpecText(difficulty),
                                              Qt::ToolTipRole);
    }
    m_defaultDifficultyCombo->setCurrentIndex(static_cast<int>(preferences.defaultEngineDifficulty));
    formOf(engine)->addRow("Default difficulty", m_defaultDifficultyCombo);

    m_showAnalysisCheck = new QCheckBox("Show analysis panel", engine);
    m_showAnalysisCheck->setChecked(preferences.showAnalysis);
    formOf(engine)->addRow(QString(), m_showAnalysisCheck);

    m_showThinkingCheck = new QCheckBox("Show engine thinking indicator", engine);
    m_showThinkingCheck->setChecked(preferences.showThinkingIndicator);
    formOf(engine)->addRow(QString(), m_showThinkingCheck);
    tabs->addTab(engine, QIcon(":/icons/difficulty.svg"), "Engine");

    root->addWidget(tabs, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
}

UiPreferences SettingsDialog::preferences() const
{
    UiPreferences prefs;
    prefs.darkTheme = m_themeCombo->currentIndex() == 0;
    prefs.boardTheme = static_cast<ChessBoardWidget::BoardTheme>(m_boardCombo->currentIndex());
    prefs.pieceStyle = m_pieceCombo->currentText();
    prefs.coordinatesVisible = m_coordinatesCheck->isChecked();
    prefs.soundsEnabled = m_soundsCheck->isChecked();
    prefs.animationsEnabled = m_animationsCheck->isChecked();
    prefs.legalMoveHints = m_legalMoveHintsCheck->isChecked();
    prefs.autoQueenPromotion = m_autoQueenCheck->isChecked();
    prefs.confirmResign = m_confirmResignCheck->isChecked();
    prefs.showAnalysis = m_showAnalysisCheck->isChecked();
    prefs.showThinkingIndicator = m_showThinkingCheck->isChecked();
    prefs.defaultEngineDifficulty = static_cast<EngineDifficulty>(m_defaultDifficultyCombo->currentIndex());
    return prefs;
}
