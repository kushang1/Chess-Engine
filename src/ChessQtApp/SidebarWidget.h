#pragma once

#include "MoveListWidget.h"

#include <QLabel>
#include <QWidget>

class EvaluationBarWidget : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(double score READ score WRITE setScore)

public:
    explicit EvaluationBarWidget(QWidget* parent = nullptr);

    QSize sizeHint() const override;
    double score() const;
    void setScore(double score);
    void animateToScore(double score);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    double m_score = 0.0;
};

class SidebarWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SidebarWidget(QWidget* parent = nullptr);

    MoveListWidget* moveListWidget() const;
    void clearMoves();
    void addMove(bool whiteMove, const QString& san);
    void setCurrentPly(int ply);
    void truncateMovesToPly(int ply);
    void setClockTimes(qint64 whiteMs, qint64 blackMs);
    void setActivePlayer(bool whiteActive);
    void setStatusText(const QString& status);
    void setThinking(bool thinking);
    void setEvaluation(double score);
    void setSearchSummary(const QString& summary);
    void setMoveListEnabled(bool enabled);
    void setModeText(const QString& mode);
    void setPlayerInfo(const QString& whiteName,
                       const QString& whiteSubtitle,
                       const QString& blackName,
                       const QString& blackSubtitle);
    void setMaterialSummary(int whiteMaterial, int blackMaterial);
    void setAnalysisDetails(const QString& bestMove,
                            double evaluation,
                            int depth,
                            const QString& difficulty);

signals:
    void positionRequested(int positionIndex);

private:
    QWidget* createPlayerCard(const QString& name, const QString& subtitle, bool white);
    void polish(QWidget* widget);
    QString formatTime(qint64 milliseconds) const;

    QLabel* m_whiteClock = nullptr;
    QLabel* m_blackClock = nullptr;
    QLabel* m_whiteName = nullptr;
    QLabel* m_blackName = nullptr;
    QLabel* m_whiteSubtitle = nullptr;
    QLabel* m_blackSubtitle = nullptr;
    QLabel* m_whiteMaterial = nullptr;
    QLabel* m_blackMaterial = nullptr;
    QWidget* m_whiteCard = nullptr;
    QWidget* m_blackCard = nullptr;
    QLabel* m_modeLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_searchLabel = nullptr;
    QLabel* m_thinkingLabel = nullptr;
    MoveListWidget* m_moveList = nullptr;
    EvaluationBarWidget* m_evalBar = nullptr;
};
