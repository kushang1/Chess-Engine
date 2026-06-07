#include "MainWindow.h"

#include "NewGameDialog.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDir>
#include <QFile>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QRandomGenerator>
#include <QShortcut>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextEdit>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

bool isWhitePiece(Piece piece)
{
    return piece >= WQ && piece <= WB;
}

bool isKing(Piece piece, bool white)
{
    return white ? piece == WK : piece == BK;
}

int pieceValue(Piece piece)
{
    switch (piece) {
    case WP: case BP: return 1;
    case WN: case BN: return 3;
    case WB: case BB: return 3;
    case WR: case BR: return 5;
    case WQ: case BQ: return 9;
    default: return 0;
    }
}

double materialScore(const std::array<Piece, 64>& board)
{
    int score = 0;
    for (Piece piece : board) {
        if (piece == EMPTY) {
            continue;
        }
        score += isWhitePiece(piece) ? pieceValue(piece) : -pieceValue(piece);
    }
    return static_cast<double>(score);
}

QString squareName(int square)
{
    if (square < 0 || square >= 64) {
        return {};
    }
    return QString("%1%2")
        .arg(QChar('a' + (square & 7)))
        .arg(8 - (square >> 3));
}

QString darkStyleSheet()
{
    return R"(
QMainWindow {
    background: #101113;
    color: #EAF2F8;
}
#CentralShell {
    background: #101113;
}
#BoardStage {
    background: #171A1F;
    border: 1px solid #282D35;
    border-radius: 12px;
}
#CenterStatus {
    color: #EEF6FA;
    font-size: 18px;
    font-weight: 700;
    padding: 4px 8px;
}
QToolBar {
    background: #171A1F;
    border: none;
    border-bottom: 1px solid #262B33;
    spacing: 8px;
    padding: 7px 12px;
}
QToolBar::separator {
    background: #303640;
    width: 1px;
    margin: 7px 6px;
}
QToolButton {
    background: transparent;
    border: none;
    border-radius: 9px;
    padding: 7px;
}
QToolButton:hover {
    background: #252B33;
}
QToolButton:pressed {
    background: #293D3A;
}
QToolButton:checked {
    background: #21463F;
}
QToolButton:disabled {
    background: transparent;
    opacity: 0.45;
}
QDockWidget {
    color: #EAF2F8;
    titlebar-close-icon: none;
    titlebar-normal-icon: none;
}
QDockWidget::title {
    background: #171A1F;
    padding: 8px 12px;
    text-align: left;
    font-weight: 700;
}
#Sidebar {
    background: #181B20;
}
#Panel, #PlayerCard, #DialogPanel, #ChoiceCard {
    background: #22262D;
    border: 1px solid #303741;
    border-radius: 10px;
}
#ChoiceCard:hover {
    border-color: #4E655F;
}
#PlayerCard[active="true"] {
    border: 1px solid #6BE2C4;
    background: #23302E;
}
#ModeLabel {
    background: #111418;
    color: #C7D2DE;
    border: 1px solid #303741;
    border-radius: 10px;
    padding: 8px 10px;
    font-weight: 800;
}
#LightAvatar, #DarkAvatar {
    border-radius: 21px;
    font-weight: 800;
}
#LightAvatar {
    background: #F2F5F7;
    color: #14181D;
}
#DarkAvatar {
    background: #0B0D10;
    color: #EAF2F8;
    border: 1px solid #3D4652;
}
#PlayerName, #ChoiceTitle {
    color: #F4F8FB;
    font-size: 14px;
    font-weight: 800;
}
#MutedLabel {
    color: #98A5B3;
    font-size: 12px;
}
#MaterialLabel {
    color: #6BE2C4;
    font-weight: 800;
}
#SectionLabel {
    color: #EAF2F8;
    font-size: 13px;
    font-weight: 800;
    padding-bottom: 2px;
}
#ClockLabel {
    background: #111318;
    color: #F8FAFC;
    border-radius: 8px;
    padding: 7px 9px;
    font-family: Consolas, "Cascadia Mono", monospace;
    font-size: 20px;
    font-weight: 800;
}
#ClockLabel[active="true"] {
    border: 1px solid #6BE2C4;
    color: #FFFFFF;
}
#ClockLabel[low="true"] {
    color: #FF6B7C;
}
#StatusLabel {
    background: #141820;
    color: #DDE7EF;
    border: 1px solid #303741;
    border-radius: 10px;
    padding: 9px 12px;
    font-weight: 700;
}
#PillLabel {
    background: #121416;
    color: #94A3B8;
    border-radius: 8px;
    padding: 7px 10px;
    font-weight: 700;
}
#PillLabel[thinking="true"] {
    color: #6BE2C4;
    background: #17312D;
}
#MoveListWidget {
    background: #1A1E24;
    alternate-background-color: #20252C;
    color: #EAF2F8;
    border: none;
    border-radius: 8px;
    selection-background-color: #2F6259;
    selection-color: #FFFFFF;
    font-size: 13px;
}
#MoveListWidget QHeaderView::section {
    background: #252B33;
    color: #A7B4C2;
    border: none;
    padding: 6px;
    font-size: 12px;
    font-weight: 700;
}
#MoveListWidget::item {
    padding: 5px 8px;
    border-radius: 6px;
}
QScrollBar:vertical {
    background: transparent;
    width: 10px;
    margin: 2px;
}
QScrollBar::handle:vertical {
    background: #3A4149;
    border-radius: 5px;
}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
    height: 0px;
}
QSplitter::handle {
    background: transparent;
    height: 8px;
}
QDialog, #SettingsDialog, #NewGameDialog {
    background: #1B1F25;
    color: #EAF2F8;
}
#DialogTitle {
    font-size: 22px;
    font-weight: 900;
}
QComboBox, QCheckBox, QRadioButton, QTextEdit {
    color: #EAF2F8;
    background: #252A32;
    border: 1px solid #39414D;
    border-radius: 8px;
    padding: 7px 9px;
}
QRadioButton, QCheckBox {
    border: none;
    background: transparent;
}
QComboBox:hover, QTextEdit:hover {
    background: #2A3039;
}
QComboBox:focus, QTextEdit:focus {
    border: 1px solid #6BE2C4;
}
QPushButton {
    background: #2C5A53;
    color: #FFFFFF;
    border: none;
    border-radius: 8px;
    padding: 8px 14px;
    font-weight: 700;
}
QPushButton:hover {
    background: #347064;
}
QPushButton:disabled {
    background: #2A3038;
    color: #7E8A97;
}
QStatusBar {
    background: #171A1F;
    color: #A7B4C2;
    border-top: 1px solid #262B33;
}
QTabWidget::pane {
    border: 1px solid #303741;
    border-radius: 10px;
    background: #1B1F25;
}
QTabBar::tab {
    background: #252A32;
    color: #AEB9C5;
    padding: 8px 14px;
    border-top-left-radius: 8px;
    border-top-right-radius: 8px;
}
QTabBar::tab:selected {
    color: #FFFFFF;
    background: #2C5A53;
}
#CentralShell {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #080D14, stop:0.58 #0D131D, stop:1 #101A21);
}
#BoardStage {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #121A24, stop:1 #0D141D);
    border: 1px solid #263343;
    border-radius: 22px;
}
#StageHeader {
    background: transparent;
}
#CenterStatus {
    color: #F8FBFF;
    font-size: 20px;
    font-weight: 800;
    padding: 2px 4px;
}
#StageBadge {
    color: #BDFEF0;
    background: #153A38;
    border: 1px solid #2B6E67;
    border-radius: 10px;
    padding: 7px 12px;
    font-size: 11px;
    font-weight: 800;
}
QToolBar {
    background: #0E151F;
    border-bottom: 1px solid #263343;
    spacing: 5px;
    padding: 8px 14px;
}
QToolButton {
    color: #CFD8E5;
    border-radius: 10px;
    padding: 7px 10px;
    font-size: 12px;
    font-weight: 700;
}
QToolButton:hover {
    color: #FFFFFF;
    background: #1A2633;
}
QToolButton:checked, QToolButton:pressed {
    color: #D9FFF7;
    background: #173A37;
}
#BrandMark {
    margin-right: 4px;
}
#BrandName {
    color: #F8FBFF;
    font-size: 15px;
    font-weight: 900;
    letter-spacing: 1px;
}
#BrandSubtitle, #MetricLabel {
    color: #718096;
    font-size: 10px;
    font-weight: 800;
}
#ToolbarSpacer {
    background: transparent;
}
#EngineLab {
    background: #0E151F;
}
#AnalysisDock::title, #GameDock::title {
    background: #0E151F;
    color: #8796A9;
    border-top: 1px solid #263343;
}
#Panel, #PlayerCard, #DialogPanel, #ChoiceCard, #MetricCard {
    background: #131C27;
    border: 1px solid #273546;
    border-radius: 14px;
}
#PlayerCard[active="true"] {
    border: 1px solid #55E4C3;
    background: #152B2B;
}
#ModeLabel {
    background: #0D141D;
    color: #B8C6D8;
    border: 1px solid #273546;
    border-radius: 12px;
    padding: 9px 11px;
}
#StatusLabel {
    background: #101923;
    border: 1px solid #273546;
    border-radius: 12px;
}
#EyebrowLabel {
    color: #55E4C3;
    font-size: 10px;
    font-weight: 900;
    letter-spacing: 2px;
}
#ModeCard, #DifficultyCard, #PromotionButton {
    text-align: left;
    color: #DCE6F2;
    background: #131C27;
    border: 1px solid #2B3A4C;
    border-radius: 14px;
    padding: 12px;
}
#ModeCard:hover, #DifficultyCard:hover, #PromotionButton:hover {
    background: #192634;
    border-color: #4D756F;
}
#ModeCard:checked, #DifficultyCard:checked {
    color: #FFFFFF;
    background: #173430;
    border: 2px solid #55E4C3;
}
#DifficultyCard[tier="4"]:checked {
    background: #34253E;
    border-color: #C58BFF;
}
#DifficultyDetails {
    color: #AEBCCD;
    background: #0F1721;
    border-radius: 10px;
    padding: 10px 12px;
}
#EngineHeadline {
    color: #F7FAFF;
    font-size: 17px;
    font-weight: 850;
}
#EngineProfile {
    color: #8F9DB0;
    font-size: 11px;
}
#MetricValue {
    color: #F7FAFF;
    font-size: 19px;
    font-weight: 850;
}
#PrimaryButton {
    background: #55E4C3;
    color: #07110F;
    padding: 10px 20px;
    font-weight: 900;
}
#PrimaryButton:hover {
    background: #75F0D4;
}
QComboBox {
    min-height: 23px;
}
QRadioButton {
    padding: 6px 10px;
    border-radius: 9px;
}
QRadioButton:hover {
    background: #182431;
}
QRadioButton::indicator:checked {
    background: #55E4C3;
    border: 3px solid #173430;
}
)";
}

QString lightStyleSheet()
{
    return R"(
QMainWindow {
    background: #F4F6F8;
    color: #151719;
}
#CentralShell {
    background: #F4F6F8;
}
#BoardStage {
    background: #FFFFFF;
    border: 1px solid #DCE3EA;
    border-radius: 12px;
}
#CenterStatus {
    color: #151719;
    font-size: 18px;
    font-weight: 700;
    padding: 4px 8px;
}
QToolBar {
    background: #FFFFFF;
    border: none;
    border-bottom: 1px solid #DDE4EA;
    spacing: 8px;
    padding: 7px 12px;
}
QToolBar::separator {
    background: #DDE4EA;
    width: 1px;
    margin: 7px 6px;
}
QToolButton {
    background: transparent;
    border: none;
    border-radius: 9px;
    padding: 7px;
}
QToolButton:hover {
    background: #ECEFF3;
}
QToolButton:pressed, QToolButton:checked {
    background: #DCEFEA;
}
QToolButton:disabled {
    background: transparent;
    opacity: 0.45;
}
QDockWidget::title {
    background: #FFFFFF;
    padding: 8px 12px;
    text-align: left;
    font-weight: 700;
}
#Sidebar {
    background: #FFFFFF;
}
#Panel, #PlayerCard, #DialogPanel, #ChoiceCard {
    background: #FFFFFF;
    border: 1px solid #DDE4EA;
    border-radius: 10px;
}
#ChoiceCard:hover {
    border-color: #8AC7BA;
}
#PlayerCard[active="true"] {
    border: 1px solid #208F7B;
    background: #EAF7F4;
}
#ModeLabel {
    background: #F0F3F6;
    color: #26313D;
    border: 1px solid #DDE4EA;
    border-radius: 10px;
    padding: 8px 10px;
    font-weight: 800;
}
#LightAvatar, #DarkAvatar {
    border-radius: 21px;
    font-weight: 800;
}
#LightAvatar {
    background: #F2F5F7;
    color: #14181D;
}
#DarkAvatar {
    background: #161A20;
    color: #FFFFFF;
}
#PlayerName, #ChoiceTitle {
    color: #111318;
    font-size: 14px;
    font-weight: 800;
}
#MutedLabel {
    color: #687586;
    font-size: 12px;
}
#MaterialLabel {
    color: #208F7B;
    font-weight: 800;
}
#SectionLabel {
    color: #111318;
    font-size: 13px;
    font-weight: 800;
    padding-bottom: 2px;
}
#ClockLabel {
    background: #F0F3F6;
    color: #111318;
    border-radius: 8px;
    padding: 7px 9px;
    font-family: Consolas, "Cascadia Mono", monospace;
    font-size: 20px;
    font-weight: 800;
}
#ClockLabel[active="true"] {
    border: 1px solid #208F7B;
}
#ClockLabel[low="true"] {
    color: #D7263D;
}
#StatusLabel {
    background: #F0F3F6;
    color: #1F2933;
    border: 1px solid #DDE4EA;
    border-radius: 10px;
    padding: 9px 12px;
    font-weight: 700;
}
#PillLabel {
    background: #F0F3F6;
    color: #687586;
    border-radius: 8px;
    padding: 7px 10px;
    font-weight: 700;
}
#PillLabel[thinking="true"] {
    color: #166B5E;
    background: #DDF3EE;
}
#MoveListWidget {
    background: #F8FAFC;
    alternate-background-color: #EEF3F6;
    color: #14181D;
    border: none;
    border-radius: 8px;
    selection-background-color: #BEE7DE;
    selection-color: #101418;
    font-size: 13px;
}
#MoveListWidget QHeaderView::section {
    background: #EEF2F6;
    color: #5A6776;
    border: none;
    padding: 6px;
    font-size: 12px;
    font-weight: 700;
}
#MoveListWidget::item {
    padding: 5px 8px;
    border-radius: 6px;
}
QScrollBar:vertical {
    background: transparent;
    width: 10px;
    margin: 2px;
}
QScrollBar::handle:vertical {
    background: #C8D1DA;
    border-radius: 5px;
}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
    height: 0px;
}
QSplitter::handle {
    background: transparent;
    height: 8px;
}
QDialog, #SettingsDialog, #NewGameDialog {
    background: #FFFFFF;
    color: #151719;
}
#DialogTitle {
    font-size: 22px;
    font-weight: 900;
}
QComboBox, QCheckBox, QRadioButton, QTextEdit {
    color: #151719;
    background: #F8FAFC;
    border: 1px solid #DDE4EA;
    border-radius: 8px;
    padding: 7px 9px;
}
QRadioButton, QCheckBox {
    border: none;
    background: transparent;
}
QComboBox:hover, QTextEdit:hover {
    background: #EEF2F6;
}
QComboBox:focus, QTextEdit:focus {
    border: 1px solid #208F7B;
}
QPushButton {
    background: #208F7B;
    color: #FFFFFF;
    border: none;
    border-radius: 8px;
    padding: 8px 14px;
    font-weight: 700;
}
QPushButton:hover {
    background: #28A58E;
}
QPushButton:disabled {
    background: #E6EBF0;
    color: #8A96A3;
}
QStatusBar {
    background: #FFFFFF;
    color: #687586;
    border-top: 1px solid #DDE4EA;
}
QTabWidget::pane {
    border: 1px solid #DDE4EA;
    border-radius: 10px;
    background: #FFFFFF;
}
QTabBar::tab {
    background: #EEF2F6;
    color: #687586;
    padding: 8px 14px;
    border-top-left-radius: 8px;
    border-top-right-radius: 8px;
}
QTabBar::tab:selected {
    color: #FFFFFF;
    background: #208F7B;
}
#CentralShell {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #E9F0F5, stop:0.6 #F4F7FA, stop:1 #EAF5F2);
}
#BoardStage {
    background: #FFFFFF;
    border: 1px solid #D4E0E8;
    border-radius: 22px;
}
#StageHeader {
    background: transparent;
}
#CenterStatus {
    color: #10202A;
    font-size: 20px;
    font-weight: 800;
    padding: 2px 4px;
}
#StageBadge {
    color: #0D6758;
    background: #DDF5EF;
    border: 1px solid #A9DED2;
    border-radius: 10px;
    padding: 7px 12px;
    font-size: 11px;
    font-weight: 800;
}
QToolBar {
    background: #FFFFFF;
    border-bottom: 1px solid #D8E3EA;
    spacing: 5px;
    padding: 8px 14px;
}
QToolButton {
    color: #425466;
    border-radius: 10px;
    padding: 7px 10px;
    font-size: 12px;
    font-weight: 700;
}
QToolButton:hover {
    color: #12212A;
    background: #EDF3F6;
}
QToolButton:checked, QToolButton:pressed {
    color: #0D6758;
    background: #DDF5EF;
}
#BrandName {
    color: #10202A;
    font-size: 15px;
    font-weight: 900;
    letter-spacing: 1px;
}
#BrandSubtitle, #MetricLabel {
    color: #7D8D9B;
    font-size: 10px;
    font-weight: 800;
}
#ToolbarSpacer {
    background: transparent;
}
#EngineLab {
    background: #F7FAFC;
}
#AnalysisDock::title, #GameDock::title {
    background: #FFFFFF;
    color: #758694;
    border-top: 1px solid #D8E3EA;
}
#Panel, #PlayerCard, #DialogPanel, #ChoiceCard, #MetricCard {
    background: #FFFFFF;
    border: 1px solid #D8E3EA;
    border-radius: 14px;
}
#PlayerCard[active="true"] {
    border: 1px solid #27A88E;
    background: #ECF9F5;
}
#ModeLabel, #StatusLabel {
    background: #F1F6F8;
    border: 1px solid #D8E3EA;
    border-radius: 12px;
}
#EyebrowLabel {
    color: #15846F;
    font-size: 10px;
    font-weight: 900;
    letter-spacing: 2px;
}
#ModeCard, #DifficultyCard, #PromotionButton {
    text-align: left;
    color: #20313C;
    background: #F8FAFC;
    border: 1px solid #D7E2E9;
    border-radius: 14px;
    padding: 12px;
}
#ModeCard:hover, #DifficultyCard:hover, #PromotionButton:hover {
    background: #F0F7F5;
    border-color: #80C9BA;
}
#ModeCard:checked, #DifficultyCard:checked {
    color: #102A24;
    background: #DDF5EF;
    border: 2px solid #27A88E;
}
#DifficultyCard[tier="4"]:checked {
    background: #F4EAFE;
    border-color: #9A64CA;
}
#DifficultyDetails {
    color: #536675;
    background: #F2F6F8;
    border-radius: 10px;
    padding: 10px 12px;
}
#EngineHeadline {
    color: #10202A;
    font-size: 17px;
    font-weight: 850;
}
#EngineProfile {
    color: #758694;
    font-size: 11px;
}
#MetricValue {
    color: #10202A;
    font-size: 19px;
    font-weight: 850;
}
#PrimaryButton {
    background: #15846F;
    color: #FFFFFF;
    padding: 10px 20px;
    font-weight: 900;
}
#PrimaryButton:hover {
    background: #1B9C84;
}
QComboBox {
    min-height: 23px;
}
QRadioButton {
    padding: 6px 10px;
    border-radius: 9px;
}
QRadioButton:hover {
    background: #EDF3F6;
}
QRadioButton::indicator:checked {
    background: #15846F;
    border: 3px solid #DDF5EF;
}
)";
}

void appendLe16(QByteArray& data, qint16 value)
{
    data.append(static_cast<char>(value & 0xFF));
    data.append(static_cast<char>((value >> 8) & 0xFF));
}

void appendLe32(QByteArray& data, quint32 value)
{
    data.append(static_cast<char>(value & 0xFF));
    data.append(static_cast<char>((value >> 8) & 0xFF));
    data.append(static_cast<char>((value >> 16) & 0xFF));
    data.append(static_cast<char>((value >> 24) & 0xFF));
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    m_appSettings = AppSettings::load();
    m_engineController = new EngineController(this);

    buildInterface();
    setupSounds();
    applyTheme();
    startGame(m_appSettings.game);

    connect(&m_clockTimer, &QTimer::timeout, this, &MainWindow::updateClocks);
    m_clockTimer.start(100);
}

MainWindow::~MainWindow()
{
    m_engineController->stopSearch();
}

void MainWindow::buildInterface()
{
    setWindowTitle("Nova Chess Studio");
    setWindowIcon(QIcon(":/icons/logo.svg"));
    resize(1380, 880);
    setMinimumSize(1120, 720);
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks | QMainWindow::AllowTabbedDocks);

    m_stack = new QStackedWidget(this);
    setCentralWidget(m_stack);

    auto* gamePage = new QWidget(this);
    gamePage->setObjectName("CentralShell");
    auto* root = new QVBoxLayout(gamePage);
    root->setContentsMargins(18, 18, 18, 18);
    root->setSpacing(12);

    auto* boardStage = new QWidget(gamePage);
    boardStage->setObjectName("BoardStage");
    auto* boardLayout = new QVBoxLayout(boardStage);
    boardLayout->setContentsMargins(20, 16, 20, 20);
    boardLayout->setSpacing(12);

    auto* stageHeader = new QWidget(boardStage);
    stageHeader->setObjectName("StageHeader");
    auto* stageHeaderLayout = new QHBoxLayout(stageHeader);
    stageHeaderLayout->setContentsMargins(2, 0, 2, 0);
    stageHeaderLayout->setSpacing(12);

    m_centerStatus = new QLabel("White to move", stageHeader);
    m_centerStatus->setObjectName("CenterStatus");
    m_centerStatus->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    stageHeaderLayout->addWidget(m_centerStatus, 1);

    m_stageBadge = new QLabel("ADVANCED", stageHeader);
    m_stageBadge->setObjectName("StageBadge");
    m_stageBadge->setAlignment(Qt::AlignCenter);
    stageHeaderLayout->addWidget(m_stageBadge);
    boardLayout->addWidget(stageHeader);

    m_board = new ChessBoardWidget(boardStage);
    boardLayout->addWidget(m_board, 1);
    root->addWidget(boardStage, 1);
    m_stack->addWidget(gamePage);

    connect(m_board, &ChessBoardWidget::squareClicked, this, &MainWindow::handleSquareClicked);
    connect(m_board, &ChessBoardWidget::dragStarted, this, &MainWindow::handleDragStarted);
    connect(m_board, &ChessBoardWidget::moveRequested, this, &MainWindow::handleMoveRequested);
    connect(m_board, &ChessBoardWidget::moveAnimationStarted, this, [this]() {
        m_moveAnimationInProgress = true;
        updateBoardInputState();
    });
    connect(m_board, &ChessBoardWidget::moveAnimationFinished, this, [this]() {
        m_moveAnimationInProgress = false;
        updateBoardInputState();
        maybeStartEngineTurn();
    });
    connect(m_engineController, &EngineController::searchStarted,
            this, &MainWindow::handleEngineSearchStarted);
    connect(m_engineController, &EngineController::searchFinished,
            this, &MainWindow::handleEngineSearchFinished);

    buildToolbar();
    buildSidebarDock();
    buildBottomDock();
    statusBar()->showMessage("Ready");

    auto* redoAltShortcut = new QShortcut(QKeySequence("Ctrl+Shift+Z"), this);
    connect(redoAltShortcut, &QShortcut::activated, this, &MainWindow::redoMove);
    auto* previousMoveShortcut = new QShortcut(QKeySequence(Qt::Key_Left), this);
    connect(previousMoveShortcut, &QShortcut::activated, this, &MainWindow::undoMove);
    auto* nextMoveShortcut = new QShortcut(QKeySequence(Qt::Key_Right), this);
    connect(nextMoveShortcut, &QShortcut::activated, this, &MainWindow::redoMove);
}

void MainWindow::buildToolbar()
{
    auto* toolbar = addToolBar("Controls");
    toolbar->setObjectName("CommandBar");
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    toolbar->setIconSize(QSize(20, 20));
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    auto* brand = new QWidget(toolbar);
    auto* brandLayout = new QHBoxLayout(brand);
    brandLayout->setContentsMargins(0, 0, 14, 0);
    brandLayout->setSpacing(8);
    auto* brandMark = new QLabel(brand);
    brandMark->setObjectName("BrandMark");
    brandMark->setPixmap(QIcon(":/icons/logo.svg").pixmap(32, 32));
    brandLayout->addWidget(brandMark);
    auto* brandCopy = new QVBoxLayout();
    brandCopy->setContentsMargins(0, 0, 0, 0);
    brandCopy->setSpacing(0);
    auto* brandName = new QLabel("NOVA CHESS", brand);
    brandName->setObjectName("BrandName");
    auto* brandSubtitle = new QLabel("PLAY. STUDY. MASTER.", brand);
    brandSubtitle->setObjectName("BrandSubtitle");
    brandCopy->addWidget(brandName);
    brandCopy->addWidget(brandSubtitle);
    brandLayout->addLayout(brandCopy);
    toolbar->addWidget(brand);
    toolbar->addSeparator();

    m_newGameAction = toolbar->addAction(QIcon(":/icons/new-game.svg"), "New Game");
    m_newGameAction->setShortcut(QKeySequence("Ctrl+N"));
    m_newGameAction->setToolTip("New Game (Ctrl+N)");
    m_newGameAction->setStatusTip("Choose a game mode and start a fresh game.");

    m_undoAction = toolbar->addAction(QIcon(":/icons/undo.svg"), "Undo");
    m_undoAction->setShortcut(QKeySequence("Ctrl+Z"));
    m_undoAction->setToolTip("Undo (Ctrl+Z)");
    m_undoAction->setStatusTip("Go back one move.");

    m_redoAction = toolbar->addAction(QIcon(":/icons/redo.svg"), "Redo");
    m_redoAction->setShortcut(QKeySequence("Ctrl+Y"));
    m_redoAction->setToolTip("Redo (Ctrl+Y)");
    m_redoAction->setStatusTip("Go forward one move.");

    toolbar->addSeparator();

    m_flipAction = toolbar->addAction(QIcon(":/icons/flip.svg"), "Flip Board");
    m_flipAction->setShortcut(QKeySequence("Ctrl+F"));
    m_flipAction->setToolTip("Flip Board (Ctrl+F)");
    m_flipAction->setStatusTip("Rotate the board.");

    m_analysisAction = toolbar->addAction(QIcon(":/icons/analysis.svg"), "Analysis");
    m_analysisAction->setCheckable(true);
    m_analysisAction->setToolTip("Show Analysis");
    m_analysisAction->setStatusTip("Show or hide the live Engine Lab.");

    toolbar->addSeparator();

    m_resignAction = toolbar->addAction(QIcon(":/icons/resign.svg"), "Resign");
    m_resignAction->setToolTip("Resign");
    m_resignAction->setStatusTip("Resign the current game.");

    auto* spacer = new QWidget(toolbar);
    spacer->setObjectName("ToolbarSpacer");
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);

    m_themeAction = toolbar->addAction(QIcon(":/icons/theme.svg"), "Theme");
    m_themeAction->setCheckable(true);
    m_themeAction->setChecked(m_appSettings.ui.darkTheme);
    m_themeAction->setToolTip("Toggle light and dark theme");

    m_soundAction = toolbar->addAction(QIcon(":/icons/sound.svg"), "Sound");
    m_soundAction->setCheckable(true);
    m_soundAction->setChecked(m_appSettings.ui.soundsEnabled);
    m_soundAction->setToolTip("Toggle move sounds");

    m_settingsAction = toolbar->addAction(QIcon(":/icons/settings.svg"), "Settings");
    m_settingsAction->setShortcut(QKeySequence("Ctrl+,"));
    m_settingsAction->setToolTip("Settings (Ctrl+,)");
    m_settingsAction->setStatusTip("Open appearance, gameplay, and engine preferences.");

    connect(m_newGameAction, &QAction::triggered, this, &MainWindow::newGame);
    connect(m_undoAction, &QAction::triggered, this, &MainWindow::undoMove);
    connect(m_redoAction, &QAction::triggered, this, &MainWindow::redoMove);
    connect(m_resignAction, &QAction::triggered, this, &MainWindow::resignGame);
    connect(m_settingsAction, &QAction::triggered, this, &MainWindow::openSettings);
    connect(m_flipAction, &QAction::triggered, this, &MainWindow::flipBoard);
    connect(m_analysisAction, &QAction::triggered, this, &MainWindow::toggleAnalysisDock);
    connect(m_themeAction, &QAction::triggered, this, &MainWindow::toggleTheme);
    connect(m_soundAction, &QAction::triggered, this, &MainWindow::toggleSound);
}

void MainWindow::buildSidebarDock()
{
    m_sidebar = new SidebarWidget(this);
    connect(m_sidebar, &SidebarWidget::positionRequested,
            this, &MainWindow::handleHistoryPositionRequested);
    connect(m_sidebar->moveListWidget(), &MoveListWidget::previousMoveRequested,
            this, &MainWindow::undoMove);
    connect(m_sidebar->moveListWidget(), &MoveListWidget::nextMoveRequested,
            this, &MainWindow::redoMove);

    m_sidebarDock = new QDockWidget("Match Center", this);
    m_sidebarDock->setObjectName("GameDock");
    m_sidebarDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_sidebarDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    m_sidebarDock->setWidget(m_sidebar);
    addDockWidget(Qt::RightDockWidgetArea, m_sidebarDock);
    resizeDocks(QList<QDockWidget*>{ m_sidebarDock }, QList<int>{ 360 }, Qt::Horizontal);
    animatePanel(m_sidebar);
}

void MainWindow::buildBottomDock()
{
    auto* lab = new QWidget(this);
    lab->setObjectName("EngineLab");
    auto* root = new QHBoxLayout(lab);
    root->setContentsMargins(18, 14, 18, 14);
    root->setSpacing(16);

    auto* summary = new QWidget(lab);
    summary->setMinimumWidth(270);
    auto* summaryLayout = new QVBoxLayout(summary);
    summaryLayout->setContentsMargins(0, 2, 12, 2);
    summaryLayout->setSpacing(4);
    auto* eyebrow = new QLabel("ENGINE LAB", summary);
    eyebrow->setObjectName("EyebrowLabel");
    summaryLayout->addWidget(eyebrow);
    m_engineHeadline = new QLabel("Ready for the first move", summary);
    m_engineHeadline->setObjectName("EngineHeadline");
    summaryLayout->addWidget(m_engineHeadline);
    m_engineProfile = new QLabel(summary);
    m_engineProfile->setObjectName("EngineProfile");
    m_engineProfile->setWordWrap(true);
    summaryLayout->addWidget(m_engineProfile);
    summaryLayout->addStretch();
    root->addWidget(summary, 2);

    auto createMetric = [lab](const QString& title, QLabel*& value) {
        auto* card = new QWidget(lab);
        card->setObjectName("MetricCard");
        auto* layout = new QVBoxLayout(card);
        layout->setContentsMargins(14, 11, 14, 11);
        layout->setSpacing(3);
        auto* label = new QLabel(title, card);
        label->setObjectName("MetricLabel");
        value = new QLabel("-", card);
        value->setObjectName("MetricValue");
        layout->addWidget(label);
        layout->addWidget(value);
        layout->addStretch();
        return card;
    };

    root->addWidget(createMetric("DEPTH", m_engineDepthMetric), 1);
    root->addWidget(createMetric("NODES", m_engineNodesMetric), 1);
    root->addWidget(createMetric("THINK TIME", m_engineTimeMetric), 1);
    root->addWidget(createMetric("SPEED", m_engineSpeedMetric), 1);

    m_bottomDock = new QDockWidget("Engine Lab", this);
    m_bottomDock->setObjectName("AnalysisDock");
    m_bottomDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
    m_bottomDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    m_bottomDock->setWidget(lab);
    addDockWidget(Qt::BottomDockWidgetArea, m_bottomDock);
    resizeDocks(QList<QDockWidget*>{ m_bottomDock }, QList<int>{ 180 }, Qt::Vertical);
    m_bottomDock->setVisible(m_appSettings.ui.showAnalysis);
    m_analysisAction->setChecked(m_appSettings.ui.showAnalysis);
    resetEngineLab();
}

void MainWindow::applyTheme()
{
    qApp->setStyleSheet(m_appSettings.ui.darkTheme ? darkStyleSheet() : lightStyleSheet());
    if (m_themeAction) {
        m_themeAction->setChecked(m_appSettings.ui.darkTheme);
    }
    if (m_soundAction) {
        m_soundAction->setChecked(m_appSettings.ui.soundsEnabled);
    }
    if (m_board) {
        m_board->setCoordinatesVisible(m_appSettings.ui.coordinatesVisible);
        m_board->setAnimationsEnabled(m_appSettings.ui.animationsEnabled);
        m_board->setBoardTheme(m_appSettings.ui.boardTheme);
    }
}

void MainWindow::animatePanel(QWidget* widget)
{
    auto* effect = new QGraphicsOpacityEffect(widget);
    widget->setGraphicsEffect(effect);

    auto* opacity = new QPropertyAnimation(effect, "opacity", widget);
    opacity->setDuration(220);
    opacity->setStartValue(0.0);
    opacity->setEndValue(1.0);
    opacity->setEasingCurve(QEasingCurve::OutCubic);
    connect(opacity, &QPropertyAnimation::finished, widget, [widget]() {
        widget->setGraphicsEffect(nullptr);
    });
    opacity->start(QAbstractAnimation::DeleteWhenStopped);
}

void MainWindow::setupSounds()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/ChessQtAppSounds";
    QDir().mkpath(base);

    const QString movePath = base + "/move.wav";
    const QString capturePath = base + "/capture.wav";
    const QString checkPath = base + "/check.wav";
    writeToneFile(movePath, 620, 70);
    writeToneFile(capturePath, 360, 95);
    writeToneFile(checkPath, 880, 120);

    m_moveSound.setSource(QUrl::fromLocalFile(movePath));
    m_captureSound.setSource(QUrl::fromLocalFile(capturePath));
    m_checkSound.setSource(QUrl::fromLocalFile(checkPath));
    m_moveSound.setVolume(0.24f);
    m_captureSound.setVolume(0.28f);
    m_checkSound.setVolume(0.30f);
}

void MainWindow::writeToneFile(const QString& path, int frequency, int durationMs)
{
    if (QFile::exists(path)) {
        return;
    }

    constexpr int sampleRate = 44100;
    constexpr int channels = 1;
    constexpr int bitsPerSample = 16;
    const int samples = sampleRate * durationMs / 1000;
    const int dataBytes = samples * channels * bitsPerSample / 8;

    QByteArray wav;
    wav.append("RIFF", 4);
    appendLe32(wav, 36 + dataBytes);
    wav.append("WAVE", 4);
    wav.append("fmt ", 4);
    appendLe32(wav, 16);
    appendLe16(wav, 1);
    appendLe16(wav, channels);
    appendLe32(wav, sampleRate);
    appendLe32(wav, sampleRate * channels * bitsPerSample / 8);
    appendLe16(wav, channels * bitsPerSample / 8);
    appendLe16(wav, bitsPerSample);
    wav.append("data", 4);
    appendLe32(wav, dataBytes);

    for (int i = 0; i < samples; ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        const double fadeIn = std::min(1.0, i / 220.0);
        const double fadeOut = std::min(1.0, (samples - i) / 500.0);
        const double envelope = std::min(fadeIn, fadeOut);
        const auto sample = static_cast<qint16>(std::sin(2.0 * 3.14159265358979323846 * frequency * t) *
                                                32767.0 * 0.22 * envelope);
        appendLe16(wav, sample);
    }

    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(wav);
    }
}

void MainWindow::newGame()
{
    NewGameDialog dialog(m_appSettings.game, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    m_appSettings.game = dialog.gameSettings();
    m_appSettings.game.engineDifficulty = m_appSettings.game.gameMode == GameMode::HumanVsEngine
        ? m_appSettings.game.engineDifficulty
        : m_appSettings.ui.defaultEngineDifficulty;
    m_appSettings.save();
    startGame(m_appSettings.game);
}

void MainWindow::startGame(const GameSettings& settings)
{
    m_engineController->stopSearch();
    ++m_gameGeneration;
    m_engineThinking = false;
    m_moveAnimationInProgress = false;
    m_gameFinished = false;
    m_selectedSquare = -1;
    m_lastFrom = -1;
    m_lastTo = -1;
    m_lastSearchDepth = 0;
    m_lastSearchMoveTimeMs = 0;
    m_lastEngineScoreCp = 0;
    m_hasEngineScore = false;
    m_lastBestMove.clear();

    m_appSettings.game = settings;
    if (m_appSettings.game.gameMode == GameMode::HumanVsEngine && m_appSettings.game.playerSide == PlayerSide::Random) {
        m_resolvedHumanSide = QRandomGenerator::global()->bounded(2) == 0 ? PlayerSide::White : PlayerSide::Black;
    }
    else if (m_appSettings.game.playerSide == PlayerSide::Black) {
        m_resolvedHumanSide = PlayerSide::Black;
    }
    else {
        m_resolvedHumanSide = PlayerSide::White;
    }

    m_engine.newGame();
    m_positionHistory.clear();
    m_repetitionHistory.clear();
    m_moveHistory.clear();
    m_currentMoveIndex = 0;
    recordCurrentPosition();

    m_sidebar->clearMoves();
    m_sidebar->setThinking(false);
    m_sidebar->setSearchSummary("Ready");
    m_sidebar->setEvaluation(0.0);
    m_sidebar->setMaterialSummary(0, 0);
    resetEngineLab();
    m_board->clearLastMove();
    m_board->clearSelection();
    m_board->setCheckSquare(-1);
    m_board->setPosition(boardSnapshot());
    m_board->setBoardFlipped(m_appSettings.game.gameMode == GameMode::HumanVsEngine &&
                             m_resolvedHumanSide == PlayerSide::Black);

    resetClocks();
    updateModeStatus();
    updateGameStatusViews();
    updateActionStates();
    updateBoardInputState();
    statusBar()->showMessage(modeStatusText());
    maybeStartEngineTurn();
}

void MainWindow::undoMove()
{
    if (m_engineThinking || m_moveAnimationInProgress || m_currentMoveIndex == 0) {
        return;
    }
    restorePosition(m_currentMoveIndex - 1);
}

void MainWindow::redoMove()
{
    if (m_engineThinking || m_moveAnimationInProgress || m_currentMoveIndex + 1 >= static_cast<int>(m_positionHistory.size())) {
        return;
    }
    restorePosition(m_currentMoveIndex + 1);
}

void MainWindow::resignGame()
{
    if (m_gameFinished || m_engineThinking) {
        return;
    }

    if (m_appSettings.ui.confirmResign) {
        const auto answer = QMessageBox::question(this, "Resign", "Resign the current game?");
        if (answer != QMessageBox::Yes) {
            return;
        }
    }

    m_gameFinished = true;
    const QString status = m_engine.isWhiteTurn() ? "White resigned" : "Black resigned";
    m_centerStatus->setText(status);
    m_sidebar->setStatusText(status);
    updateBoardInputState();
    updateActionStates();
}

void MainWindow::openSettings()
{
    SettingsDialog dialog(m_appSettings.ui, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    m_appSettings.ui = dialog.preferences();
    if (m_appSettings.game.gameMode == GameMode::HumanVsEngine) {
        m_appSettings.game.engineDifficulty = m_appSettings.ui.defaultEngineDifficulty;
    }
    m_appSettings.save();
    applyTheme();
    if (m_bottomDock) {
        m_bottomDock->setVisible(m_appSettings.ui.showAnalysis);
        m_analysisAction->setChecked(m_appSettings.ui.showAnalysis);
    }
    updateModeStatus();
    updateGameStatusViews();
}

void MainWindow::flipBoard()
{
    m_board->setBoardFlipped(!m_board->isBoardFlipped());
}

void MainWindow::toggleAnalysisDock()
{
    const bool show = m_analysisAction->isChecked();
    m_bottomDock->setVisible(show);
    m_appSettings.ui.showAnalysis = show;
    m_appSettings.save();
    if (show) {
        animatePanel(m_bottomDock->widget());
    }
}

void MainWindow::toggleTheme()
{
    m_appSettings.ui.darkTheme = m_themeAction->isChecked();
    m_appSettings.save();
    applyTheme();
}

void MainWindow::toggleSound()
{
    m_appSettings.ui.soundsEnabled = m_soundAction->isChecked();
    m_appSettings.save();
    statusBar()->showMessage(m_appSettings.ui.soundsEnabled ? "Move sounds enabled" : "Move sounds muted", 2200);
}

void MainWindow::updateBoardInputState()
{
    m_board->setBoardInputEnabled(isBoardInputAllowed());
}

void MainWindow::updateActionStates()
{
    m_newGameAction->setEnabled(true);
    m_undoAction->setEnabled(!m_engineThinking && !m_moveAnimationInProgress && m_currentMoveIndex > 0);
    m_redoAction->setEnabled(!m_engineThinking &&
                             !m_moveAnimationInProgress &&
                             m_currentMoveIndex + 1 < static_cast<int>(m_positionHistory.size()));
    m_resignAction->setEnabled(!m_engineThinking && !m_gameFinished);
    m_flipAction->setEnabled(true);
    m_settingsAction->setEnabled(true);
    m_themeAction->setEnabled(true);
    m_soundAction->setEnabled(true);
}

bool MainWindow::isBoardInputAllowed() const
{
    return !m_engineThinking &&
           !m_moveAnimationInProgress &&
           !m_gameFinished &&
           isLatestPosition() &&
           isHumanSideToMove();
}

bool MainWindow::isEngineTurn() const
{
    if (m_appSettings.game.gameMode != GameMode::HumanVsEngine) {
        return false;
    }

    const bool humanIsWhite = m_resolvedHumanSide == PlayerSide::White;
    return m_engine.isWhiteTurn() != humanIsWhite;
}

bool MainWindow::isHumanSideToMove() const
{
    if (m_appSettings.game.gameMode == GameMode::HumanVsHuman) {
        return true;
    }
    return !isEngineTurn();
}

bool MainWindow::isLatestPosition() const
{
    return !m_positionHistory.empty() &&
           m_currentMoveIndex == static_cast<int>(m_positionHistory.size()) - 1;
}

void MainWindow::maybeStartEngineTurn()
{
    if (m_appSettings.game.gameMode != GameMode::HumanVsEngine ||
        m_gameFinished ||
        m_engineThinking ||
        m_moveAnimationInProgress ||
        !isLatestPosition() ||
        !isEngineTurn()) {
        updateBoardInputState();
        updateActionStates();
        return;
    }

    m_engineController->startSearch(m_engine.currentFen(),
                                    m_repetitionHistory,
                                    m_appSettings.game.engineDifficulty,
                                    m_gameGeneration);
}

void MainWindow::handleEngineSearchStarted(int generation)
{
    if (generation != m_gameGeneration) {
        return;
    }

    m_engineThinking = true;
    if (m_appSettings.ui.showThinkingIndicator) {
        m_sidebar->setThinking(true);
    }
    m_sidebar->setSearchSummary(QString("Searching\nDepth: %1\nLevel: %2")
                                .arg(difficultyDepth(m_appSettings.game.engineDifficulty))
                                .arg(difficultyText(m_appSettings.game.engineDifficulty)));
    m_engineHeadline->setText("Calculating the strongest continuation");
    m_engineProfile->setText(difficultyDescription(m_appSettings.game.engineDifficulty) + "\n" +
                             difficultySpecText(m_appSettings.game.engineDifficulty));
    m_engineDepthMetric->setText("...");
    m_engineNodesMetric->setText("...");
    m_engineTimeMetric->setText("...");
    m_engineSpeedMetric->setText("...");
    m_centerStatus->setText("Engine thinking...");
    statusBar()->showMessage("Engine thinking...");
    updateBoardInputState();
    updateActionStates();
}

void MainWindow::handleEngineSearchFinished(int generation,
                                            Move bestMove,
                                            long long nodes,
                                            long long leafNodes,
                                            int depth,
                                            int elapsedMs,
                                            int bestScore,
                                            int threads)
{
    if (generation != m_gameGeneration) {
        return;
    }

    m_engineThinking = false;
    m_sidebar->setThinking(false);
    m_lastSearchDepth = depth;
    m_lastSearchMoveTimeMs = elapsedMs;

    if (bestMove.from == -1 || !isEngineTurn()) {
        m_gameFinished = true;
        m_centerStatus->setText("Engine has no legal move");
        m_sidebar->setStatusText("Engine has no legal move");
        updateBoardInputState();
        updateActionStates();
        return;
    }

    const bool engineWasWhite = m_engine.isWhiteTurn();
    const QString san = QString::fromStdString(m_engine.moveToSan(bestMove));
    const QString uci = moveText(bestMove);
    m_lastBestMove = uci;
    m_lastEngineScoreCp = engineWasWhite ? bestScore : -bestScore;
    m_hasEngineScore = true;
    if (!m_engine.makeMove(bestMove)) {
        m_gameFinished = true;
        m_centerStatus->setText("Engine returned an illegal move");
        m_sidebar->setStatusText("Engine returned an illegal move");
        updateBoardInputState();
        updateActionStates();
        return;
    }

    m_sidebar->setSearchSummary(QString("Best: %1\nDepth: %2\nNodes: %3\nTime: %4 ms")
                                .arg(uci)
                                .arg(depth)
                                .arg(nodes)
                                .arg(elapsedMs));
    Q_UNUSED(leafNodes);
    updateEngineLab(uci, nodes, depth, elapsedMs, threads, m_lastEngineScoreCp);
    completeMove(bestMove, engineWasWhite, san, true);
    statusBar()->showMessage(modeStatusText());
}

void MainWindow::handleSquareClicked(int square)
{
    if (!isBoardInputAllowed()) {
        return;
    }

    if (m_selectedSquare == -1) {
        selectSquare(square);
        return;
    }

    if (square == m_selectedSquare) {
        clearSelection();
        return;
    }

    if (tryMakeMove(m_selectedSquare, square)) {
        return;
    }

    selectSquare(square);
}

void MainWindow::handleDragStarted(int square)
{
    if (isBoardInputAllowed()) {
        selectSquare(square);
    }
}

void MainWindow::handleMoveRequested(int from, int to)
{
    if (!isBoardInputAllowed()) {
        return;
    }

    if (!tryMakeMove(from, to)) {
        clearSelection();
    }
}

void MainWindow::handleHistoryPositionRequested(int positionIndex)
{
    if (m_engineThinking || positionIndex < 0) {
        return;
    }
    restorePosition(std::min(positionIndex, static_cast<int>(m_positionHistory.size()) - 1));
}

void MainWindow::selectSquare(int square)
{
    if (!isBoardInputAllowed() || square < 0 || square >= 64) {
        clearSelection();
        return;
    }

    const Piece piece = m_engine.pieceAt(square);
    if (piece == EMPTY || isWhitePiece(piece) != m_engine.isWhiteTurn()) {
        clearSelection();
        return;
    }

    m_selectedSquare = square;
    m_board->setSelectedSquare(square);
    if (m_appSettings.ui.legalMoveHints) {
        m_board->setLegalMoves(legalMovesFrom(square));
    }
}

void MainWindow::clearSelection()
{
    m_selectedSquare = -1;
    m_board->clearSelection();
}

std::vector<Move> MainWindow::legalMovesFrom(int square) const
{
    std::vector<Move> moves;
    for (const Move& move : m_engine.legalMoves()) {
        if (move.from == square) {
            moves.push_back(move);
        }
    }
    return moves;
}

bool MainWindow::tryMakeMove(int from, int to)
{
    if (!isBoardInputAllowed()) {
        return false;
    }

    std::vector<Move> matchingMoves;
    for (const Move& move : m_engine.legalMoves()) {
        if (move.from == from && move.to == to) {
            matchingMoves.push_back(move);
        }
    }

    if (matchingMoves.empty()) {
        return false;
    }

    Move selectedMove = matchingMoves.front();
    if (matchingMoves.size() > 1 && !m_appSettings.ui.autoQueenPromotion) {
        bool accepted = false;
        selectedMove = choosePromotionMove(matchingMoves, accepted);
        if (!accepted) {
            return false;
        }
    }
    else {
        const auto queen = std::find_if(matchingMoves.begin(), matchingMoves.end(), [](const Move& move) {
            return move.promotedTo == WQ || move.promotedTo == BQ;
        });
        if (queen != matchingMoves.end()) {
            selectedMove = *queen;
        }
    }

    truncateHistory();
    const bool whiteMove = m_engine.isWhiteTurn();
    const QString san = QString::fromStdString(m_engine.moveToSan(selectedMove));
    if (!m_engine.makeMove(selectedMove)) {
        clearSelection();
        return false;
    }

    completeMove(selectedMove, whiteMove, san, true);
    return true;
}

Move MainWindow::choosePromotionMove(const std::vector<Move>& moves, bool& accepted)
{
    accepted = false;
    Move selection = moves.empty() ? Move{} : moves.front();

    QDialog dialog(this);
    dialog.setObjectName("PromotionDialog");
    dialog.setWindowTitle("Choose Promotion");
    dialog.setModal(true);
    dialog.setMinimumWidth(520);

    auto* root = new QVBoxLayout(&dialog);
    root->setContentsMargins(22, 22, 22, 18);
    root->setSpacing(14);

    auto* eyebrow = new QLabel("PAWN PROMOTION", &dialog);
    eyebrow->setObjectName("EyebrowLabel");
    root->addWidget(eyebrow);
    auto* title = new QLabel("Choose your new piece", &dialog);
    title->setObjectName("DialogTitle");
    root->addWidget(title);

    auto* choices = new QHBoxLayout();
    choices->setSpacing(10);
    for (const Move& move : moves) {
        QString name;
        QString resource;
        switch (move.promotedTo) {
        case WQ: name = "Queen"; resource = ":/pieces/white_queen.png"; break;
        case WR: name = "Rook"; resource = ":/pieces/white_rook.png"; break;
        case WB: name = "Bishop"; resource = ":/pieces/white_bishop.png"; break;
        case WN: name = "Knight"; resource = ":/pieces/white_knight.png"; break;
        case BQ: name = "Queen"; resource = ":/pieces/black_queen.png"; break;
        case BR: name = "Rook"; resource = ":/pieces/black_rook.png"; break;
        case BB: name = "Bishop"; resource = ":/pieces/black_bishop.png"; break;
        case BN: name = "Knight"; resource = ":/pieces/black_knight.png"; break;
        default: continue;
        }

        auto* button = new QPushButton(QIcon(resource), name, &dialog);
        button->setObjectName("PromotionButton");
        button->setIconSize(QSize(42, 42));
        button->setMinimumHeight(76);
        button->setCursor(Qt::PointingHandCursor);
        choices->addWidget(button, 1);
        connect(button, &QPushButton::clicked, &dialog, [&, move]() {
            selection = move;
            accepted = true;
            dialog.accept();
        });
    }
    root->addLayout(choices);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    root->addWidget(buttons);
    dialog.exec();
    return selection;
}

void MainWindow::completeMove(const Move& move, bool whiteMove, const QString& san, bool animate)
{
    clearSelection();
    m_lastFrom = move.from;
    m_lastTo = move.to;
    refreshBoard(animate, move.from, move.to);

    m_moveHistory.push_back(move);
    recordCurrentPosition();
    m_currentMoveIndex = static_cast<int>(m_positionHistory.size()) - 1;
    addMoveToHistory(whiteMove, san);
    m_sidebar->setCurrentPly(m_currentMoveIndex);
    playMoveFeedback(move);
    if (m_appSettings.game.gameMode == GameMode::HumanVsEngine && isEngineTurn()) {
        m_hasEngineScore = false;
        m_lastBestMove.clear();
        m_lastSearchDepth = 0;
    }
    updateGameStatusViews();

    if (!checkDrawByRepetitionOr50()) {
        updateGameStatusLabel();
    }

    updateBoardInputState();
    updateActionStates();
    maybeStartEngineTurn();
}

std::array<Piece, 64> MainWindow::boardSnapshot() const
{
    std::array<Piece, 64> snapshot{};
    for (int square = 0; square < 64; ++square) {
        snapshot[square] = m_engine.pieceAt(square);
    }
    return snapshot;
}

void MainWindow::refreshBoard(bool animate, int from, int to)
{
    const std::array<Piece, 64> snapshot = boardSnapshot();
    if (from >= 0 && to >= 0) {
        m_board->setLastMove(from, to);
    }
    else {
        m_board->clearLastMove();
    }

    m_board->setCheckSquare(checkedKingSquare());

    if (animate) {
        m_board->animateMove(from, to, snapshot);
    }
    else {
        m_board->setPosition(snapshot);
    }
}

void MainWindow::updateGameStatusViews()
{
    m_activeClockWhite = m_engine.isWhiteTurn();
    m_sidebar->setActivePlayer(m_activeClockWhite);
    const std::array<Piece, 64> board = boardSnapshot();
    const double eval = m_hasEngineScore
        ? static_cast<double>(m_lastEngineScoreCp) / 100.0
        : materialScore(board);
    m_sidebar->setEvaluation(eval);
    m_sidebar->setMaterialSummary(materialForWhite(true), materialForWhite(false));
    m_sidebar->setAnalysisDetails(m_lastBestMove,
                                  eval,
                                  m_lastSearchDepth,
                                  difficultyText(m_appSettings.game.engineDifficulty));
    updateGameStatusLabel();
    updateModeStatus();
}

bool MainWindow::checkDrawByRepetitionOr50()
{
    const uint64_t currentHash = m_engine.positionHash();
    int count = 0;
    for (uint64_t hash : m_repetitionHistory) {
        if (hash == currentHash) {
            ++count;
        }
    }

    if (count >= 3) {
        m_gameFinished = true;
        m_centerStatus->setText("Draw by threefold repetition");
        m_sidebar->setStatusText("Draw by threefold repetition");
        updateBoardInputState();
        return true;
    }

    if (m_engine.gameStatus().kind == chess::GameStatusKind::FiftyMoveRule) {
        m_gameFinished = true;
        m_centerStatus->setText("Draw by 50-move rule");
        m_sidebar->setStatusText("Draw by 50-move rule");
        updateBoardInputState();
        return true;
    }

    return false;
}

bool MainWindow::updateGameStatusLabel()
{
    const chess::GameStatus status = m_engine.gameStatus();
    QString text;

    switch (status.kind) {
    case chess::GameStatusKind::Checkmate:
        m_gameFinished = true;
        text = status.whiteToMove ? "Checkmate - Black wins" : "Checkmate - White wins";
        break;
    case chess::GameStatusKind::Stalemate:
        m_gameFinished = true;
        text = "Draw by stalemate";
        break;
    case chess::GameStatusKind::ThreefoldRepetition:
        m_gameFinished = true;
        text = "Draw by threefold repetition";
        break;
    case chess::GameStatusKind::FiftyMoveRule:
        m_gameFinished = true;
        text = "Draw by 50-move rule";
        break;
    case chess::GameStatusKind::Ongoing:
    default:
        text = m_engine.isWhiteTurn() ? "White to move" : "Black to move";
        if (status.inCheck) {
            text += " - check";
        }
        break;
    }

    m_centerStatus->setText(text);
    m_sidebar->setStatusText(text);
    m_board->setCheckSquare(checkedKingSquare());
    statusBar()->showMessage(modeStatusText() + " | " + text);
    updateBoardInputState();
    return status.kind != chess::GameStatusKind::Ongoing;
}

void MainWindow::updateModeStatus()
{
    if (m_appSettings.game.gameMode == GameMode::HumanVsHuman) {
        m_sidebar->setModeText("Human vs Human");
        m_sidebar->setPlayerInfo("Local Player", "White", "Local Player", "Black");
        m_stageBadge->setText("LOCAL MATCH");
        return;
    }

    const QString humanSide = playerSideText(m_resolvedHumanSide);
    m_sidebar->setModeText(QString("Human vs Engine - %1").arg(difficultyText(m_appSettings.game.engineDifficulty)));
    m_stageBadge->setText(difficultyText(m_appSettings.game.engineDifficulty).toUpper());
    if (m_resolvedHumanSide == PlayerSide::White) {
        m_sidebar->setPlayerInfo("You", "White", "Engine", "Black");
    }
    else {
        m_sidebar->setPlayerInfo("Engine", "White", "You", "Black");
    }
    statusBar()->showMessage(QString("Human vs Engine - %1 - You are %2")
                             .arg(difficultyText(m_appSettings.game.engineDifficulty), humanSide));
}

QString MainWindow::modeStatusText() const
{
    if (m_appSettings.game.gameMode == GameMode::HumanVsHuman) {
        return "Human vs Human";
    }
    return QString("Human vs Engine - %1 - You are %2")
        .arg(difficultyText(m_appSettings.game.engineDifficulty),
             playerSideText(m_resolvedHumanSide));
}

QString MainWindow::moveText(const Move& move) const
{
    QString text = squareName(move.from) + squareName(move.to);
    if (move.wasPromotion) {
        text += "q";
    }
    return text;
}

int MainWindow::checkedKingSquare() const
{
    const chess::GameStatus status = m_engine.gameStatus();
    if (!status.inCheck || status.kind != chess::GameStatusKind::Ongoing) {
        return -1;
    }

    for (int square = 0; square < 64; ++square) {
        if (isKing(m_engine.pieceAt(square), m_engine.isWhiteTurn())) {
            return square;
        }
    }
    return -1;
}

int MainWindow::materialForWhite(bool white) const
{
    int material = 0;
    const auto board = boardSnapshot();
    for (Piece piece : board) {
        if (piece != EMPTY && isWhitePiece(piece) == white) {
            material += pieceValue(piece);
        }
    }
    return material;
}

void MainWindow::playMoveFeedback(const Move& move)
{
    if (!m_appSettings.ui.soundsEnabled) {
        return;
    }

    const chess::GameStatus status = m_engine.gameStatus();
    if (status.inCheck) {
        m_checkSound.play();
    }
    else if (move.captured != EMPTY || move.wasEnPassant) {
        m_captureSound.play();
    }
    else {
        m_moveSound.play();
    }
}

void MainWindow::updateClocks()
{
    if (!m_clockElapsed.isValid()) {
        m_clockElapsed.start();
        return;
    }

    const qint64 elapsed = m_clockElapsed.restart();
    if (!m_gameFinished && !m_unlimitedTime) {
        if (m_activeClockWhite) {
            m_whiteRemainingMs = std::max<qint64>(0, m_whiteRemainingMs - elapsed);
            if (m_whiteRemainingMs == 0) {
                m_gameFinished = true;
                m_centerStatus->setText("White lost on time");
                m_sidebar->setStatusText("White lost on time");
            }
        }
        else {
            m_blackRemainingMs = std::max<qint64>(0, m_blackRemainingMs - elapsed);
            if (m_blackRemainingMs == 0) {
                m_gameFinished = true;
                m_centerStatus->setText("Black lost on time");
                m_sidebar->setStatusText("Black lost on time");
            }
        }
    }

    m_sidebar->setClockTimes(m_whiteRemainingMs, m_blackRemainingMs);
    updateBoardInputState();
    updateActionStates();
}

void MainWindow::resetClocks()
{
    m_unlimitedTime = m_appSettings.game.initialTimeMs < 0;
    m_whiteRemainingMs = m_appSettings.game.initialTimeMs;
    m_blackRemainingMs = m_appSettings.game.initialTimeMs;
    if (m_unlimitedTime) {
        m_whiteRemainingMs = -1;
        m_blackRemainingMs = -1;
    }
    m_activeClockWhite = true;
    m_clockElapsed.restart();
    m_sidebar->setClockTimes(m_whiteRemainingMs, m_blackRemainingMs);
    m_sidebar->setActivePlayer(true);
}

void MainWindow::resetEngineLab()
{
    if (!m_engineHeadline) {
        return;
    }

    const bool engineGame = m_appSettings.game.gameMode == GameMode::HumanVsEngine;
    m_engineHeadline->setText(engineGame ? "Ready for the first move" : "Local match in progress");
    m_engineProfile->setText(engineGame
        ? difficultyDescription(m_appSettings.game.engineDifficulty) + "\n" +
          difficultySpecText(m_appSettings.game.engineDifficulty)
        : "Engine search is paused during a local two-player match.");
    m_engineDepthMetric->setText("-");
    m_engineNodesMetric->setText("-");
    m_engineTimeMetric->setText("-");
    m_engineSpeedMetric->setText("-");
}

void MainWindow::updateEngineLab(const QString& bestMove,
                                 long long nodes,
                                 int depth,
                                 int elapsedMs,
                                 int threads,
                                 int scoreCp)
{
    auto compactNumber = [](long long value) {
        if (value >= 1000000000LL) {
            return QString("%1B").arg(value / 1000000000.0, 0, 'f', 1);
        }
        if (value >= 1000000LL) {
            return QString("%1M").arg(value / 1000000.0, 0, 'f', 1);
        }
        if (value >= 1000LL) {
            return QString("%1K").arg(value / 1000.0, 0, 'f', 1);
        }
        return QString::number(value);
    };

    const long long nodesPerSecond = elapsedMs > 0 ? (nodes * 1000LL) / elapsedMs : 0;
    const QString score = std::abs(scoreCp) >= 90000
        ? QString("mate")
        : QString("%1%2").arg(scoreCp >= 0 ? "+" : "").arg(scoreCp / 100.0, 0, 'f', 2);

    m_engineHeadline->setText(QString("Best line %1  |  Eval %2").arg(bestMove, score));
    m_engineProfile->setText(QString("%1 strength completed the search with %2 active thread%3.")
                             .arg(difficultyText(m_appSettings.game.engineDifficulty))
                             .arg(threads)
                             .arg(threads == 1 ? "" : "s"));
    m_engineDepthMetric->setText(QString::number(depth));
    m_engineNodesMetric->setText(compactNumber(nodes));
    m_engineTimeMetric->setText(elapsedMs >= 1000
        ? QString("%1s").arg(elapsedMs / 1000.0, 0, 'f', 2)
        : QString("%1ms").arg(elapsedMs));
    m_engineSpeedMetric->setText(compactNumber(nodesPerSecond) + "/s");
}

void MainWindow::recordCurrentPosition()
{
    m_positionHistory.push_back(m_engine.currentFen());
    m_repetitionHistory.push_back(m_engine.positionHash());
}

void MainWindow::restorePosition(int positionIndex)
{
    if (positionIndex < 0 || positionIndex >= static_cast<int>(m_positionHistory.size())) {
        return;
    }

    m_currentMoveIndex = positionIndex;
    m_engine.setPositionFromFen(m_positionHistory[m_currentMoveIndex]);
    clearSelection();
    m_gameFinished = false;
    m_hasEngineScore = false;
    m_lastBestMove.clear();
    m_lastSearchDepth = 0;
    resetEngineLab();

    if (m_currentMoveIndex > 0 && m_currentMoveIndex - 1 < static_cast<int>(m_moveHistory.size())) {
        m_lastFrom = m_moveHistory[m_currentMoveIndex - 1].from;
        m_lastTo = m_moveHistory[m_currentMoveIndex - 1].to;
        refreshBoard(false, m_lastFrom, m_lastTo);
    }
    else {
        m_lastFrom = -1;
        m_lastTo = -1;
        refreshBoard(false);
    }

    m_sidebar->setCurrentPly(m_currentMoveIndex);
    updateGameStatusViews();
    updateActionStates();
    updateBoardInputState();
}

void MainWindow::truncateHistory()
{
    if (m_currentMoveIndex + 1 >= static_cast<int>(m_positionHistory.size())) {
        return;
    }

    m_positionHistory.resize(m_currentMoveIndex + 1);
    m_repetitionHistory.resize(m_currentMoveIndex + 1);
    m_moveHistory.resize(m_currentMoveIndex);
    m_sidebar->truncateMovesToPly(m_currentMoveIndex);
}

void MainWindow::addMoveToHistory(bool whiteMove, const QString& san)
{
    m_sidebar->addMove(whiteMove, san);
}
