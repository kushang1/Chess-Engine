#include "NewGameDialog.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

namespace {

QRadioButton* makeRadio(const QString& text, QWidget* parent)
{
    auto* radio = new QRadioButton(text, parent);
    radio->setCursor(Qt::PointingHandCursor);
    return radio;
}

qint64 timeControlMsFromIndex(int index)
{
    switch (index) {
    case 0: return -1;
    case 1: return 5 * 60 * 1000;
    case 2: return 10 * 60 * 1000;
    case 3: return 15 * 60 * 1000;
    default: return 5 * 60 * 1000;
    }
}

int timeControlIndex(qint64 milliseconds)
{
    if (milliseconds < 0) {
        return 0;
    }
    if (milliseconds == 10 * 60 * 1000) {
        return 2;
    }
    if (milliseconds == 15 * 60 * 1000) {
        return 3;
    }
    return 1;
}

} // namespace

NewGameDialog::NewGameDialog(const GameSettings& settings, QWidget* parent)
    : QDialog(parent)
    , m_initialSettings(settings)
{
    setObjectName("NewGameDialog");
    setWindowTitle("New Game");
    setModal(true);
    resize(500, 460);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(22, 22, 22, 22);
    root->setSpacing(16);

    auto* title = new QLabel("New Game", this);
    title->setObjectName("DialogTitle");
    root->addWidget(title);

    auto* subtitle = new QLabel("Choose the match type and engine setup.", this);
    subtitle->setObjectName("MutedLabel");
    root->addWidget(subtitle);

    m_modeGroup = new QButtonGroup(this);
    m_modeGroup->setExclusive(true);

    auto* modeLayout = new QHBoxLayout();
    modeLayout->setSpacing(12);
    modeLayout->addWidget(createModeCard("Human vs Human", "Two local players share the board.", static_cast<int>(GameMode::HumanVsHuman)));
    modeLayout->addWidget(createModeCard("Human vs Engine", "Play one side against the engine.", static_cast<int>(GameMode::HumanVsEngine)));
    root->addLayout(modeLayout);

    m_enginePanel = new QWidget(this);
    m_enginePanel->setObjectName("DialogPanel");
    auto* engineLayout = new QVBoxLayout(m_enginePanel);
    engineLayout->setContentsMargins(14, 14, 14, 14);
    engineLayout->setSpacing(12);

    auto* sideLabel = new QLabel("Your side", m_enginePanel);
    sideLabel->setObjectName("SectionLabel");
    engineLayout->addWidget(sideLabel);

    m_sideGroup = new QButtonGroup(this);
    m_sideGroup->setExclusive(true);
    auto* sideLayout = new QHBoxLayout();
    sideLayout->setSpacing(8);
    auto* white = makeRadio("White", m_enginePanel);
    auto* black = makeRadio("Black", m_enginePanel);
    auto* random = makeRadio("Random", m_enginePanel);
    m_sideGroup->addButton(white, static_cast<int>(PlayerSide::White));
    m_sideGroup->addButton(black, static_cast<int>(PlayerSide::Black));
    m_sideGroup->addButton(random, static_cast<int>(PlayerSide::Random));
    sideLayout->addWidget(white);
    sideLayout->addWidget(black);
    sideLayout->addWidget(random);
    sideLayout->addStretch();
    engineLayout->addLayout(sideLayout);

    auto* difficultyLabel = new QLabel("Engine difficulty", m_enginePanel);
    difficultyLabel->setObjectName("SectionLabel");
    engineLayout->addWidget(difficultyLabel);

    m_difficultyCombo = new QComboBox(m_enginePanel);
    for (int i = 0; i <= static_cast<int>(EngineDifficulty::Expert); ++i) {
        m_difficultyCombo->addItem(difficultyText(static_cast<EngineDifficulty>(i)));
    }
    engineLayout->addWidget(m_difficultyCombo);
    root->addWidget(m_enginePanel);

    auto* timePanel = new QWidget(this);
    timePanel->setObjectName("DialogPanel");
    auto* timeLayout = new QVBoxLayout(timePanel);
    timeLayout->setContentsMargins(14, 14, 14, 14);
    timeLayout->setSpacing(10);
    auto* timeLabel = new QLabel("Time control", timePanel);
    timeLabel->setObjectName("SectionLabel");
    timeLayout->addWidget(timeLabel);
    m_timeControlCombo = new QComboBox(timePanel);
    m_timeControlCombo->addItems({ "Unlimited", "5+0", "10+0", "15+0" });
    timeLayout->addWidget(m_timeControlCombo);
    root->addWidget(timePanel);

    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setObjectName("StatusLabel");
    m_summaryLabel->setWordWrap(true);
    root->addWidget(m_summaryLabel);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    auto* startButton = buttons->addButton("Start Game", QDialogButtonBox::AcceptRole);
    startButton->setObjectName("PrimaryButton");
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    if (auto* modeButton = m_modeGroup->button(static_cast<int>(settings.gameMode))) {
        modeButton->setChecked(true);
    }
    if (auto* sideButton = m_sideGroup->button(static_cast<int>(settings.playerSide))) {
        sideButton->setChecked(true);
    }
    m_difficultyCombo->setCurrentIndex(static_cast<int>(settings.engineDifficulty));
    m_timeControlCombo->setCurrentIndex(timeControlIndex(settings.initialTimeMs));

    connect(m_modeGroup, &QButtonGroup::idClicked, this, &NewGameDialog::updateEngineControls);
    connect(m_sideGroup, &QButtonGroup::idClicked, this, &NewGameDialog::updateEngineControls);
    connect(m_difficultyCombo, &QComboBox::currentIndexChanged, this, &NewGameDialog::updateEngineControls);
    updateEngineControls();
}

GameSettings NewGameDialog::gameSettings() const
{
    GameSettings settings = m_initialSettings;
    settings.gameMode = static_cast<GameMode>(m_modeGroup->checkedId());
    settings.playerSide = static_cast<PlayerSide>(m_sideGroup->checkedId());
    settings.engineDifficulty = static_cast<EngineDifficulty>(m_difficultyCombo->currentIndex());
    settings.initialTimeMs = timeControlMsFromIndex(m_timeControlCombo->currentIndex());
    return settings;
}

QWidget* NewGameDialog::createModeCard(const QString& title, const QString& subtitle, int id)
{
    auto* card = new QWidget(this);
    card->setObjectName("ChoiceCard");
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(6);

    auto* radio = makeRadio(title, card);
    radio->setObjectName("ChoiceTitle");
    m_modeGroup->addButton(radio, id);
    layout->addWidget(radio);

    auto* text = new QLabel(subtitle, card);
    text->setObjectName("MutedLabel");
    text->setWordWrap(true);
    layout->addWidget(text);
    layout->addStretch();

    return card;
}

void NewGameDialog::updateEngineControls()
{
    const bool engineGame = static_cast<GameMode>(m_modeGroup->checkedId()) == GameMode::HumanVsEngine;
    m_enginePanel->setEnabled(engineGame);

    if (engineGame) {
        const auto side = static_cast<PlayerSide>(m_sideGroup->checkedId());
        const auto difficulty = static_cast<EngineDifficulty>(m_difficultyCombo->currentIndex());
        m_summaryLabel->setText(QString("%1 - %2 - You are %3")
                                .arg(gameModeText(GameMode::HumanVsEngine),
                                     difficultyText(difficulty),
                                     playerSideText(side)));
    }
    else {
        m_summaryLabel->setText("Human vs Human - both sides are controlled locally.");
    }
}
