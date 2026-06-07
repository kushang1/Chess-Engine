#include "NewGameDialog.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
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
    resize(780, 650);
    setMinimumWidth(720);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(22, 22, 22, 22);
    root->setSpacing(16);

    auto* eyebrow = new QLabel("MATCH SETUP", this);
    eyebrow->setObjectName("EyebrowLabel");
    root->addWidget(eyebrow);

    auto* title = new QLabel("Choose your next battle", this);
    title->setObjectName("DialogTitle");
    root->addWidget(title);

    auto* subtitle = new QLabel("Pick a mode, tune the challenge, and enter the board.", this);
    subtitle->setObjectName("MutedLabel");
    root->addWidget(subtitle);

    m_modeGroup = new QButtonGroup(this);
    m_modeGroup->setExclusive(true);

    auto* modeLayout = new QHBoxLayout();
    modeLayout->setSpacing(12);
    modeLayout->addWidget(createModeCard("Local Match",
                                         "Two players, one board",
                                         ":/icons/human-vs-human.svg",
                                         static_cast<int>(GameMode::HumanVsHuman)));
    modeLayout->addWidget(createModeCard("Challenge Engine",
                                         "Five strengths, maximum power",
                                         ":/icons/human-vs-engine.svg",
                                         static_cast<int>(GameMode::HumanVsEngine)));
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

    auto* difficultyLabel = new QLabel("Engine strength", m_enginePanel);
    difficultyLabel->setObjectName("SectionLabel");
    engineLayout->addWidget(difficultyLabel);

    m_difficultyGroup = new QButtonGroup(this);
    m_difficultyGroup->setExclusive(true);
    auto* difficultyLayout = new QHBoxLayout();
    difficultyLayout->setSpacing(8);
    for (int i = 0; i <= static_cast<int>(EngineDifficulty::Master); ++i) {
        difficultyLayout->addWidget(createDifficultyCard(static_cast<EngineDifficulty>(i)), 1);
    }
    engineLayout->addLayout(difficultyLayout);

    m_difficultyDetails = new QLabel(m_enginePanel);
    m_difficultyDetails->setObjectName("DifficultyDetails");
    m_difficultyDetails->setWordWrap(true);
    engineLayout->addWidget(m_difficultyDetails);
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
    m_timeControlCombo->addItems({ "Unlimited", "5 minutes", "10 minutes", "15 minutes" });
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
    if (auto* difficultyButton = m_difficultyGroup->button(static_cast<int>(settings.engineDifficulty))) {
        difficultyButton->setChecked(true);
    }
    m_timeControlCombo->setCurrentIndex(timeControlIndex(settings.initialTimeMs));

    connect(m_modeGroup, &QButtonGroup::idClicked, this, &NewGameDialog::updateEngineControls);
    connect(m_sideGroup, &QButtonGroup::idClicked, this, &NewGameDialog::updateEngineControls);
    connect(m_difficultyGroup, &QButtonGroup::idClicked, this, &NewGameDialog::updateEngineControls);
    connect(m_timeControlCombo, &QComboBox::currentIndexChanged, this, &NewGameDialog::updateEngineControls);
    updateEngineControls();
}

GameSettings NewGameDialog::gameSettings() const
{
    GameSettings settings = m_initialSettings;
    settings.gameMode = static_cast<GameMode>(m_modeGroup->checkedId());
    settings.playerSide = static_cast<PlayerSide>(m_sideGroup->checkedId());
    settings.engineDifficulty = static_cast<EngineDifficulty>(m_difficultyGroup->checkedId());
    settings.initialTimeMs = timeControlMsFromIndex(m_timeControlCombo->currentIndex());
    return settings;
}

QPushButton* NewGameDialog::createModeCard(const QString& title,
                                           const QString& subtitle,
                                           const QString& iconPath,
                                           int id)
{
    auto* card = new QPushButton(QIcon(iconPath), title + "\n" + subtitle, this);
    card->setObjectName("ModeCard");
    card->setCheckable(true);
    card->setCursor(Qt::PointingHandCursor);
    card->setIconSize(QSize(36, 36));
    card->setMinimumHeight(82);
    m_modeGroup->addButton(card, id);
    return card;
}

QPushButton* NewGameDialog::createDifficultyCard(EngineDifficulty difficulty)
{
    static const QStringList subtitles = {
        "Friendly",
        "Tactical",
        "Club",
        "Relentless",
        "Maximum"
    };

    const int id = static_cast<int>(difficulty);
    auto* card = new QPushButton(difficultyText(difficulty) + "\n" + subtitles.value(id), m_enginePanel);
    card->setObjectName("DifficultyCard");
    card->setProperty("tier", id);
    card->setCheckable(true);
    card->setCursor(Qt::PointingHandCursor);
    card->setMinimumHeight(68);
    card->setToolTip(difficultyDescription(difficulty) + "\n" + difficultySpecText(difficulty));
    m_difficultyGroup->addButton(card, id);
    return card;
}

void NewGameDialog::updateEngineControls()
{
    const bool engineGame = static_cast<GameMode>(m_modeGroup->checkedId()) == GameMode::HumanVsEngine;
    m_enginePanel->setEnabled(engineGame);

    if (engineGame) {
        const auto side = static_cast<PlayerSide>(m_sideGroup->checkedId());
        const auto difficulty = static_cast<EngineDifficulty>(m_difficultyGroup->checkedId());
        m_difficultyDetails->setText(QString("<b>%1</b><br>%2")
                                     .arg(difficultyDescription(difficulty),
                                          difficultySpecText(difficulty)));
        m_summaryLabel->setText(QString("%1  |  %2  |  You play %3  |  %4")
                                .arg(gameModeText(GameMode::HumanVsEngine),
                                     difficultyText(difficulty),
                                     playerSideText(side),
                                     m_timeControlCombo->currentText()));
    }
    else {
        m_difficultyDetails->setText("Engine controls are available in Challenge Engine mode.");
        m_summaryLabel->setText("Human vs Human - both sides are controlled locally.");
    }
}
