#pragma once

#include <ChessEngine/Move.h>

#include <QColor>
#include <QPoint>
#include <QPixmap>
#include <QWidget>

#include <array>
#include <bitset>
#include <vector>

class QParallelAnimationGroup;

class ChessBoardWidget : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal animationProgress READ animationProgress WRITE setAnimationProgress)
    Q_PROPERTY(qreal moveGlow READ moveGlow WRITE setMoveGlow)

public:
    enum class BoardTheme {
        Classic,
        Modern,
        Blue,
        Green,
        Wood
    };

    explicit ChessBoardWidget(QWidget* parent = nullptr);

    QSize sizeHint() const override;
    bool hasHeightForWidth() const override;
    int heightForWidth(int width) const override;

    void setPosition(const std::array<Piece, 64>& position);
    void animateMove(int from, int to, const std::array<Piece, 64>& finalPosition);
    void setLegalMoves(const std::vector<Move>& moves);
    void clearLegalMoves();
    void setSelectedSquare(int square);
    void clearSelection();
    void setLastMove(int from, int to);
    void clearLastMove();
    void setCheckSquare(int square);
    void setBoardFlipped(bool flipped);
    bool isBoardFlipped() const;
    void setCoordinatesVisible(bool visible);
    bool coordinatesVisible() const;
    void setAnimationsEnabled(bool enabled);
    bool animationsEnabled() const;
    void setBoardTheme(BoardTheme theme);
    BoardTheme boardTheme() const;
    void setInteractionLocked(bool locked);
    void setBoardInputEnabled(bool enabled);
    bool isAnimatingMove() const;

    qreal animationProgress() const;
    void setAnimationProgress(qreal progress);
    qreal moveGlow() const;
    void setMoveGlow(qreal glow);

signals:
    void squareClicked(int square);
    void moveRequested(int from, int to);
    void dragStarted(int square);
    void hoverSquareChanged(int square);
    void moveAnimationStarted();
    void moveAnimationFinished();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    struct ThemeColors {
        QColor lightTop;
        QColor lightBottom;
        QColor darkTop;
        QColor darkBottom;
        QColor border;
        QColor coordinates;
    };

    QRectF boardRect() const;
    QRectF squareRect(int square) const;
    QPointF squareCenter(int square) const;
    int squareAt(const QPointF& point) const;
    qreal squareSize() const;
    void loadPiecePixmaps();
    void drawBoard(QPainter& painter);
    void drawHighlights(QPainter& painter);
    void drawPieces(QPainter& painter);
    void drawPiece(QPainter& painter, Piece piece, const QRectF& target, qreal opacity = 1.0);
    void drawCoordinates(QPainter& painter);
    ThemeColors colors() const;
    bool isAnimatingSquare(int square) const;

    std::array<Piece, 64> m_position{};
    std::array<Piece, 64> m_pendingPosition{};
    std::array<QPixmap, 13> m_piecePixmaps{};

    std::bitset<64> m_legalTargets;
    std::bitset<64> m_captureTargets;

    int m_selectedSquare = -1;
    int m_lastFrom = -1;
    int m_lastTo = -1;
    int m_checkSquare = -1;
    bool m_flipped = false;
    bool m_coordinatesVisible = true;
    bool m_animationsEnabled = true;
    bool m_interactionLocked = false;
    BoardTheme m_boardTheme = BoardTheme::Modern;

    bool m_pressed = false;
    bool m_dragging = false;
    int m_pressSquare = -1;
    int m_dragFrom = -1;
    QPoint m_pressPos;
    QPointF m_dragPos;
    int m_hoverSquare = -1;

    QParallelAnimationGroup* m_animationGroup = nullptr;
    bool m_animating = false;
    int m_animationFrom = -1;
    int m_animationTo = -1;
    Piece m_animationPiece = EMPTY;
    qreal m_animationProgress = 1.0;
    qreal m_moveGlow = 1.0;
};
