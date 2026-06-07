#include "SidebarWidget.h"

#include <QFrame>
#include <QEasingCurve>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QSplitter>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr qint64 kSecond = 1000;
constexpr qint64 kMinute = 60 * kSecond;

QLabel* makeSectionLabel(const QString& text)
{
    auto* label = new QLabel(text);
    label->setObjectName("SectionLabel");
    return label;
}

} // namespace

EvaluationBarWidget::EvaluationBarWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("EvaluationBar");
    setMinimumWidth(34);
}

QSize EvaluationBarWidget::sizeHint() const
{
    return QSize(42, 220);
}

double EvaluationBarWidget::score() const
{
    return m_score;
}

void EvaluationBarWidget::setScore(double score)
{
    m_score = std::clamp(score, -10.0, 10.0);
    update();
}

void EvaluationBarWidget::animateToScore(double score)
{
    auto* animation = new QPropertyAnimation(this, "score", this);
    animation->setDuration(260);
    animation->setStartValue(m_score);
    animation->setEndValue(std::clamp(score, -10.0, 10.0));
    animation->setEasingCurve(QEasingCurve::OutCubic);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

void EvaluationBarWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF rect = QRectF(5, 4, width() - 10, height() - 8);
    QPainterPath clip;
    clip.addRoundedRect(rect, 8, 8);

    painter.fillPath(clip, QColor("#111318"));
    painter.save();
    painter.setClipPath(clip);

    const double whiteShare = std::clamp(0.5 + m_score / 20.0, 0.05, 0.95);
    const qreal whiteHeight = rect.height() * whiteShare;
    const QRectF whiteRect(rect.left(), rect.bottom() - whiteHeight, rect.width(), whiteHeight);

    QLinearGradient whiteGradient(whiteRect.topLeft(), whiteRect.bottomLeft());
    whiteGradient.setColorAt(0.0, QColor("#F8FAFC"));
    whiteGradient.setColorAt(1.0, QColor("#CDD7E1"));
    painter.fillRect(whiteRect, whiteGradient);

    QLinearGradient blackGradient(rect.topLeft(), rect.bottomLeft());
    blackGradient.setColorAt(0.0, QColor("#212833"));
    blackGradient.setColorAt(1.0, QColor("#05070A"));
    painter.fillRect(QRectF(rect.left(), rect.top(), rect.width(), rect.height() - whiteHeight), blackGradient);

    painter.restore();

    painter.setPen(QPen(QColor(255, 255, 255, 42), 1.0));
    painter.drawRoundedRect(rect, 8, 8);

    painter.setPen(m_score >= 0 ? QColor("#101319") : QColor("#F3F6FB"));
    QFont font = painter.font();
    font.setPointSize(8);
    font.setWeight(QFont::DemiBold);
    painter.setFont(font);
    painter.drawText(rect.adjusted(0, 0, 0, -6), Qt::AlignHCenter | Qt::AlignBottom,
                     QString::number(m_score, 'f', 1));
}

SidebarWidget::SidebarWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("Sidebar");
    setMinimumWidth(330);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    m_modeLabel = new QLabel("Human vs Engine", this);
    m_modeLabel->setObjectName("ModeLabel");
    m_modeLabel->setAlignment(Qt::AlignCenter);
    root->addWidget(m_modeLabel);

    m_blackCard = createPlayerCard("Engine", "Black", false);
    root->addWidget(m_blackCard);

    auto* centerSplitter = new QSplitter(Qt::Vertical, this);
    centerSplitter->setObjectName("SidebarSplitter");
    centerSplitter->setChildrenCollapsible(false);

    auto* movePanel = new QWidget(centerSplitter);
    movePanel->setObjectName("Panel");
    auto* moveLayout = new QVBoxLayout(movePanel);
    moveLayout->setContentsMargins(12, 12, 12, 12);
    moveLayout->setSpacing(10);
    moveLayout->addWidget(makeSectionLabel("Moves"));

    auto* moveBody = new QHBoxLayout();
    moveBody->setContentsMargins(0, 0, 0, 0);
    moveBody->setSpacing(10);
    m_evalBar = new EvaluationBarWidget(movePanel);
    m_moveList = new MoveListWidget(movePanel);
    moveBody->addWidget(m_evalBar);
    moveBody->addWidget(m_moveList, 1);
    moveLayout->addLayout(moveBody, 1);

    auto* analysisPanel = new QWidget(centerSplitter);
    analysisPanel->setObjectName("Panel");
    auto* analysisLayout = new QVBoxLayout(analysisPanel);
    analysisLayout->setContentsMargins(12, 12, 12, 12);
    analysisLayout->setSpacing(9);
    analysisLayout->addWidget(makeSectionLabel("Analysis"));
    m_searchLabel = new QLabel("Ready");
    m_searchLabel->setObjectName("MutedLabel");
    m_searchLabel->setWordWrap(true);
    analysisLayout->addWidget(m_searchLabel);
    m_thinkingLabel = new QLabel("Engine idle");
    m_thinkingLabel->setObjectName("PillLabel");
    m_thinkingLabel->setAlignment(Qt::AlignCenter);
    analysisLayout->addWidget(m_thinkingLabel);
    analysisLayout->addStretch();

    centerSplitter->addWidget(movePanel);
    centerSplitter->addWidget(analysisPanel);
    centerSplitter->setStretchFactor(0, 4);
    centerSplitter->setStretchFactor(1, 2);
    root->addWidget(centerSplitter, 1);

    m_statusLabel = new QLabel("White to move");
    m_statusLabel->setObjectName("StatusLabel");
    m_statusLabel->setAlignment(Qt::AlignCenter);
    root->addWidget(m_statusLabel);

    m_whiteCard = createPlayerCard("You", "White", true);
    root->addWidget(m_whiteCard);

    connect(m_moveList, &MoveListWidget::positionRequested,
            this, &SidebarWidget::positionRequested);
}

MoveListWidget* SidebarWidget::moveListWidget() const
{
    return m_moveList;
}

void SidebarWidget::clearMoves()
{
    m_moveList->clearMoves();
}

void SidebarWidget::addMove(bool whiteMove, const QString& san)
{
    m_moveList->addMove(whiteMove, san);
}

void SidebarWidget::setCurrentPly(int ply)
{
    m_moveList->setCurrentPly(ply);
}

void SidebarWidget::truncateMovesToPly(int ply)
{
    m_moveList->truncateToPly(ply);
}

void SidebarWidget::setClockTimes(qint64 whiteMs, qint64 blackMs)
{
    m_whiteClock->setText(formatTime(whiteMs));
    m_blackClock->setText(formatTime(blackMs));

    m_whiteClock->setProperty("low", whiteMs >= 0 && whiteMs <= 30 * kSecond);
    m_blackClock->setProperty("low", blackMs >= 0 && blackMs <= 30 * kSecond);
    polish(m_whiteClock);
    polish(m_blackClock);
}

void SidebarWidget::setActivePlayer(bool whiteActive)
{
    m_whiteCard->setProperty("active", whiteActive);
    m_blackCard->setProperty("active", !whiteActive);
    m_whiteClock->setProperty("active", whiteActive);
    m_blackClock->setProperty("active", !whiteActive);
    polish(m_whiteCard);
    polish(m_blackCard);
    polish(m_whiteClock);
    polish(m_blackClock);
}

void SidebarWidget::setStatusText(const QString& status)
{
    m_statusLabel->setText(status);
}

void SidebarWidget::setThinking(bool thinking)
{
    m_thinkingLabel->setText(thinking ? "CALCULATING" : "ENGINE READY");
    m_thinkingLabel->setProperty("thinking", thinking);
    polish(m_thinkingLabel);
}

void SidebarWidget::setEvaluation(double score)
{
    m_evalBar->animateToScore(score);
}

void SidebarWidget::setSearchSummary(const QString& summary)
{
    m_searchLabel->setText(summary);
}

void SidebarWidget::setMoveListEnabled(bool enabled)
{
    m_moveList->setEnabled(enabled);
}

void SidebarWidget::setModeText(const QString& mode)
{
    m_modeLabel->setText(mode);
}

void SidebarWidget::setPlayerInfo(const QString& whiteName,
                                  const QString& whiteSubtitle,
                                  const QString& blackName,
                                  const QString& blackSubtitle)
{
    m_whiteName->setText(whiteName);
    m_whiteSubtitle->setText(whiteSubtitle);
    m_blackName->setText(blackName);
    m_blackSubtitle->setText(blackSubtitle);
}

void SidebarWidget::setMaterialSummary(int whiteMaterial, int blackMaterial)
{
    const int diff = whiteMaterial - blackMaterial;
    m_whiteMaterial->setText(diff > 0 ? QString("+%1").arg(diff) : QString());
    m_blackMaterial->setText(diff < 0 ? QString("+%1").arg(-diff) : QString());
}

void SidebarWidget::setAnalysisDetails(const QString& bestMove,
                                       double evaluation,
                                       int depth,
                                       const QString& difficulty)
{
    m_searchLabel->setText(QString("Best: %1\nEval: %2\nDepth: %3\nLevel: %4")
                           .arg(bestMove.isEmpty() ? "-" : bestMove)
                           .arg(evaluation, 0, 'f', 1)
                           .arg(depth > 0 ? QString::number(depth) : "-")
                           .arg(difficulty));
}

QWidget* SidebarWidget::createPlayerCard(const QString& name, const QString& subtitle, bool white)
{
    auto* card = new QWidget(this);
    card->setObjectName("PlayerCard");
    card->setMinimumHeight(68);

    auto* layout = new QHBoxLayout(card);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(10);

    auto* avatar = new QLabel(card);
    avatar->setObjectName(white ? "LightAvatar" : "DarkAvatar");
    avatar->setAlignment(Qt::AlignCenter);
    avatar->setFixedSize(42, 42);
    avatar->setPixmap(QPixmap(white ? ":/pieces/white_king.png" : ":/pieces/black_king.png")
                          .scaled(34, 34, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    layout->addWidget(avatar);

    auto* textColumn = new QVBoxLayout();
    textColumn->setContentsMargins(0, 0, 0, 0);
    textColumn->setSpacing(2);
    auto* nameLabel = new QLabel(name, card);
    nameLabel->setObjectName("PlayerName");
    auto* subtitleLabel = new QLabel(subtitle, card);
    subtitleLabel->setObjectName("MutedLabel");
    textColumn->addWidget(nameLabel);
    textColumn->addWidget(subtitleLabel);
    layout->addLayout(textColumn, 1);

    auto* clock = new QLabel("05:00", card);
    clock->setObjectName("ClockLabel");
    clock->setAlignment(Qt::AlignCenter);
    clock->setMinimumWidth(86);
    layout->addWidget(clock);

    auto* material = new QLabel(card);
    material->setObjectName("MaterialLabel");
    material->setAlignment(Qt::AlignCenter);
    material->setMinimumWidth(28);
    layout->addWidget(material);

    if (white) {
        m_whiteClock = clock;
        m_whiteName = nameLabel;
        m_whiteSubtitle = subtitleLabel;
        m_whiteMaterial = material;
    }
    else {
        m_blackClock = clock;
        m_blackName = nameLabel;
        m_blackSubtitle = subtitleLabel;
        m_blackMaterial = material;
    }

    return card;
}

void SidebarWidget::polish(QWidget* widget)
{
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

QString SidebarWidget::formatTime(qint64 milliseconds) const
{
    if (milliseconds < 0) {
        return "INF";
    }

    milliseconds = std::max<qint64>(0, milliseconds);
    const qint64 minutes = milliseconds / kMinute;
    const qint64 seconds = (milliseconds % kMinute) / kSecond;
    const qint64 tenths = (milliseconds % kSecond) / 100;

    if (milliseconds < 10 * kSecond) {
        return QString("%1:%2.%3")
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'))
            .arg(tenths);
    }

    return QString("%1:%2")
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
}
