#include "ChessBoardWidget.h"

#include <QApplication>
#include <QMouseEvent>
#include <QParallelAnimationGroup>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QSizePolicy>

#include <algorithm>

namespace {

constexpr int kBoardEdge = 720;

bool hasPiece(Piece piece)
{
    return piece != EMPTY;
}

QString pieceResource(Piece piece)
{
    switch (piece) {
    case WP: return ":/pieces/white_pawn.png";
    case WR: return ":/pieces/white_rook.png";
    case WN: return ":/pieces/white_knight.png";
    case WB: return ":/pieces/white_bishop.png";
    case WQ: return ":/pieces/white_queen.png";
    case WK: return ":/pieces/white_king.png";
    case BP: return ":/pieces/black_pawn.png";
    case BR: return ":/pieces/black_rook.png";
    case BN: return ":/pieces/black_knight.png";
    case BB: return ":/pieces/black_bishop.png";
    case BQ: return ":/pieces/black_queen.png";
    case BK: return ":/pieces/black_king.png";
    default: return {};
    }
}

QColor withAlpha(QColor color, int alpha)
{
    color.setAlpha(alpha);
    return color;
}

} // namespace

ChessBoardWidget::ChessBoardWidget(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    loadPiecePixmaps();
    m_position.fill(EMPTY);
    m_pendingPosition.fill(EMPTY);
}

QSize ChessBoardWidget::sizeHint() const
{
    return QSize(kBoardEdge, kBoardEdge);
}

bool ChessBoardWidget::hasHeightForWidth() const
{
    return true;
}

int ChessBoardWidget::heightForWidth(int width) const
{
    return width;
}

void ChessBoardWidget::setPosition(const std::array<Piece, 64>& position)
{
    if (m_animationGroup) {
        m_animationGroup->stop();
        m_animationGroup->deleteLater();
        m_animationGroup = nullptr;
    }

    m_animating = false;
    m_animationProgress = 1.0;
    m_moveGlow = 1.0;
    m_position = position;
    m_pendingPosition = position;
    update();
}

void ChessBoardWidget::animateMove(int from, int to, const std::array<Piece, 64>& finalPosition)
{
    if (from < 0 || from >= 64 || to < 0 || to >= 64 || !m_animationsEnabled) {
        setPosition(finalPosition);
        return;
    }

    if (m_animationGroup) {
        m_animationGroup->stop();
        m_animationGroup->deleteLater();
        m_animationGroup = nullptr;
    }

    m_pendingPosition = finalPosition;
    m_animationFrom = from;
    m_animationTo = to;
    m_animationPiece = hasPiece(m_position[from]) ? m_position[from] : finalPosition[to];
    m_animationProgress = 0.0;
    m_moveGlow = 0.0;
    m_animating = hasPiece(m_animationPiece);

    if (!m_animating) {
        setPosition(finalPosition);
        return;
    }

    emit moveAnimationStarted();

    auto* moveAnimation = new QPropertyAnimation(this, "animationProgress");
    moveAnimation->setDuration(230);
    moveAnimation->setStartValue(0.0);
    moveAnimation->setEndValue(1.0);
    moveAnimation->setEasingCurve(QEasingCurve::OutCubic);

    auto* glowAnimation = new QPropertyAnimation(this, "moveGlow");
    glowAnimation->setDuration(260);
    glowAnimation->setStartValue(0.0);
    glowAnimation->setEndValue(1.0);
    glowAnimation->setEasingCurve(QEasingCurve::OutQuad);

    m_animationGroup = new QParallelAnimationGroup(this);
    m_animationGroup->addAnimation(moveAnimation);
    m_animationGroup->addAnimation(glowAnimation);
    connect(m_animationGroup, &QParallelAnimationGroup::finished, this, [this]() {
        m_position = m_pendingPosition;
        m_animating = false;
        m_animationFrom = -1;
        m_animationTo = -1;
        m_animationPiece = EMPTY;
        m_animationProgress = 1.0;
        m_moveGlow = 1.0;
        if (m_animationGroup) {
            m_animationGroup->deleteLater();
            m_animationGroup = nullptr;
        }
        update();
        emit moveAnimationFinished();
    });
    m_animationGroup->start();
}

void ChessBoardWidget::setLegalMoves(const std::vector<Move>& moves)
{
    m_legalTargets.reset();
    m_captureTargets.reset();

    for (const Move& move : moves) {
        if (move.to < 0 || move.to >= 64) {
            continue;
        }
        m_legalTargets.set(static_cast<std::size_t>(move.to));
        if (move.captured != EMPTY || move.wasEnPassant || hasPiece(m_position[move.to])) {
            m_captureTargets.set(static_cast<std::size_t>(move.to));
        }
    }

    update();
}

void ChessBoardWidget::clearLegalMoves()
{
    m_legalTargets.reset();
    m_captureTargets.reset();
    update();
}

void ChessBoardWidget::setSelectedSquare(int square)
{
    m_selectedSquare = square;
    update();
}

void ChessBoardWidget::clearSelection()
{
    m_selectedSquare = -1;
    clearLegalMoves();
}

void ChessBoardWidget::setLastMove(int from, int to)
{
    m_lastFrom = from;
    m_lastTo = to;
    update();
}

void ChessBoardWidget::clearLastMove()
{
    m_lastFrom = -1;
    m_lastTo = -1;
    update();
}

void ChessBoardWidget::setCheckSquare(int square)
{
    if (m_checkSquare == square) {
        return;
    }
    m_checkSquare = square;
    update();
}

void ChessBoardWidget::setBoardFlipped(bool flipped)
{
    if (m_flipped == flipped) {
        return;
    }
    m_flipped = flipped;
    update();
}

bool ChessBoardWidget::isBoardFlipped() const
{
    return m_flipped;
}

void ChessBoardWidget::setCoordinatesVisible(bool visible)
{
    if (m_coordinatesVisible == visible) {
        return;
    }
    m_coordinatesVisible = visible;
    update();
}

bool ChessBoardWidget::coordinatesVisible() const
{
    return m_coordinatesVisible;
}

void ChessBoardWidget::setAnimationsEnabled(bool enabled)
{
    m_animationsEnabled = enabled;
}

bool ChessBoardWidget::animationsEnabled() const
{
    return m_animationsEnabled;
}

void ChessBoardWidget::setBoardTheme(BoardTheme theme)
{
    if (m_boardTheme == theme) {
        return;
    }
    m_boardTheme = theme;
    update();
}

ChessBoardWidget::BoardTheme ChessBoardWidget::boardTheme() const
{
    return m_boardTheme;
}

void ChessBoardWidget::setInteractionLocked(bool locked)
{
    m_interactionLocked = locked;
    setCursor(locked ? Qt::BusyCursor : Qt::ArrowCursor);
}

void ChessBoardWidget::setBoardInputEnabled(bool enabled)
{
    setInteractionLocked(!enabled);
}

bool ChessBoardWidget::isAnimatingMove() const
{
    return m_animating;
}

qreal ChessBoardWidget::animationProgress() const
{
    return m_animationProgress;
}

void ChessBoardWidget::setAnimationProgress(qreal progress)
{
    m_animationProgress = std::clamp(progress, 0.0, 1.0);
    update();
}

qreal ChessBoardWidget::moveGlow() const
{
    return m_moveGlow;
}

void ChessBoardWidget::setMoveGlow(qreal glow)
{
    m_moveGlow = std::clamp(glow, 0.0, 1.0);
    update();
}

void ChessBoardWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    drawBoard(painter);
    drawHighlights(painter);
    drawPieces(painter);
    if (m_coordinatesVisible) {
        drawCoordinates(painter);
    }
}

void ChessBoardWidget::mousePressEvent(QMouseEvent* event)
{
    if (m_interactionLocked || event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    m_pressSquare = squareAt(event->position());
    if (m_pressSquare < 0) {
        return;
    }

    m_pressed = true;
    m_dragging = false;
    m_dragFrom = -1;
    m_pressPos = event->pos();
    m_dragPos = event->position();
}

void ChessBoardWidget::mouseMoveEvent(QMouseEvent* event)
{
    const int hovered = squareAt(event->position());
    if (hovered != m_hoverSquare) {
        m_hoverSquare = hovered;
        emit hoverSquareChanged(m_hoverSquare);
    }

    if (!m_pressed || m_interactionLocked) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    if (!m_dragging &&
        m_pressSquare >= 0 &&
        hasPiece(m_position[m_pressSquare]) &&
        (event->pos() - m_pressPos).manhattanLength() >= QApplication::startDragDistance()) {
        m_dragging = true;
        m_dragFrom = m_pressSquare;
        emit dragStarted(m_dragFrom);
    }

    if (m_dragging) {
        m_dragPos = event->position();
        update();
    }
}

void ChessBoardWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    const int releaseSquare = squareAt(event->position());

    if (!m_interactionLocked && m_pressed) {
        if (m_dragging) {
            if (releaseSquare >= 0 && releaseSquare != m_dragFrom) {
                emit moveRequested(m_dragFrom, releaseSquare);
            }
            else if (m_dragFrom >= 0) {
                emit squareClicked(m_dragFrom);
            }
        }
        else if (releaseSquare >= 0) {
            emit squareClicked(releaseSquare);
        }
    }

    m_pressed = false;
    m_dragging = false;
    m_dragFrom = -1;
    m_pressSquare = -1;
    update();
}

void ChessBoardWidget::leaveEvent(QEvent* event)
{
    if (m_hoverSquare != -1) {
        m_hoverSquare = -1;
        emit hoverSquareChanged(-1);
    }
    QWidget::leaveEvent(event);
}

QRectF ChessBoardWidget::boardRect() const
{
    const qreal margin = 14.0;
    const qreal edge = std::max<qreal>(64.0, std::min(width(), height()) - margin * 2.0);
    return QRectF((width() - edge) / 2.0, (height() - edge) / 2.0, edge, edge);
}

QRectF ChessBoardWidget::squareRect(int square) const
{
    if (square < 0 || square >= 64) {
        return {};
    }

    const QRectF board = boardRect();
    const qreal cell = board.width() / 8.0;
    const int row = square / 8;
    const int col = square % 8;
    const int displayRow = m_flipped ? 7 - row : row;
    const int displayCol = m_flipped ? 7 - col : col;
    return QRectF(board.left() + displayCol * cell,
                  board.top() + displayRow * cell,
                  cell,
                  cell);
}

QPointF ChessBoardWidget::squareCenter(int square) const
{
    return squareRect(square).center();
}

int ChessBoardWidget::squareAt(const QPointF& point) const
{
    const QRectF board = boardRect();
    if (!board.contains(point)) {
        return -1;
    }

    const qreal cell = board.width() / 8.0;
    int displayCol = static_cast<int>((point.x() - board.left()) / cell);
    int displayRow = static_cast<int>((point.y() - board.top()) / cell);
    displayCol = std::clamp(displayCol, 0, 7);
    displayRow = std::clamp(displayRow, 0, 7);

    const int row = m_flipped ? 7 - displayRow : displayRow;
    const int col = m_flipped ? 7 - displayCol : displayCol;
    return row * 8 + col;
}

qreal ChessBoardWidget::squareSize() const
{
    return boardRect().width() / 8.0;
}

void ChessBoardWidget::loadPiecePixmaps()
{
    for (int i = 0; i < static_cast<int>(m_piecePixmaps.size()); ++i) {
        m_piecePixmaps[i] = QPixmap(pieceResource(static_cast<Piece>(i)));
    }
}

void ChessBoardWidget::drawBoard(QPainter& painter)
{
    const QRectF board = boardRect();
    const ThemeColors theme = colors();

    QPainterPath shadow;
    shadow.addRoundedRect(board.adjusted(5, 8, 5, 10), 18, 18);
    painter.fillPath(shadow, QColor(0, 0, 0, 64));

    QPainterPath clipPath;
    clipPath.addRoundedRect(board, 16, 16);
    painter.save();
    painter.setClipPath(clipPath);

    for (int square = 0; square < 64; ++square) {
        const int row = square / 8;
        const int col = square % 8;
        const bool light = ((row + col) % 2 == 0);
        const QRectF rect = squareRect(square);
        QLinearGradient gradient(rect.topLeft(), rect.bottomRight());
        gradient.setColorAt(0.0, light ? theme.lightTop : theme.darkTop);
        gradient.setColorAt(1.0, light ? theme.lightBottom : theme.darkBottom);
        painter.fillRect(rect, gradient);
    }

    painter.restore();

    QPen border(theme.border, 2.0);
    painter.setPen(border);
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(board.adjusted(1, 1, -1, -1), 16, 16);
}

void ChessBoardWidget::drawHighlights(QPainter& painter)
{
    const qreal cell = squareSize();
    const qreal glowOpacity = 0.28 + 0.18 * m_moveGlow;

    auto fillRounded = [&](int square, const QColor& color) {
        if (square < 0 || square >= 64) {
            return;
        }
        const QRectF rect = squareRect(square).adjusted(2, 2, -2, -2);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawRoundedRect(rect, std::max<qreal>(6.0, cell * 0.08), std::max<qreal>(6.0, cell * 0.08));
    };

    if (m_lastFrom >= 0) {
        fillRounded(m_lastFrom, QColor(255, 222, 89, static_cast<int>(255 * glowOpacity)));
    }
    if (m_lastTo >= 0) {
        fillRounded(m_lastTo, QColor(255, 222, 89, static_cast<int>(255 * glowOpacity)));
    }

    if (m_selectedSquare >= 0) {
        const QRectF rect = squareRect(m_selectedSquare).adjusted(4, 4, -4, -4);
        painter.setPen(QPen(QColor(107, 226, 196, 230), std::max<qreal>(3.0, cell * 0.045)));
        painter.setBrush(QColor(107, 226, 196, 45));
        painter.drawRoundedRect(rect, std::max<qreal>(7.0, cell * 0.1), std::max<qreal>(7.0, cell * 0.1));
    }

    if (m_checkSquare >= 0) {
        const QRectF rect = squareRect(m_checkSquare).adjusted(4, 4, -4, -4);
        QRadialGradient checkGlow(rect.center(), rect.width() * 0.7);
        checkGlow.setColorAt(0.0, QColor(232, 93, 117, 185));
        checkGlow.setColorAt(1.0, QColor(232, 93, 117, 28));
        painter.setPen(QPen(QColor(255, 170, 185, 210), std::max<qreal>(3.0, cell * 0.04)));
        painter.setBrush(checkGlow);
        painter.drawRoundedRect(rect, std::max<qreal>(7.0, cell * 0.1), std::max<qreal>(7.0, cell * 0.1));
    }

    painter.setRenderHint(QPainter::Antialiasing, true);
    for (int square = 0; square < 64; ++square) {
        if (!m_legalTargets.test(static_cast<std::size_t>(square))) {
            continue;
        }
        const QRectF rect = squareRect(square);
        if (m_captureTargets.test(static_cast<std::size_t>(square))) {
            painter.setPen(QPen(QColor(35, 35, 35, 96), std::max<qreal>(4.0, cell * 0.06)));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(rect.center(), cell * 0.39, cell * 0.39);
            continue;
        }

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(25, 25, 25, 88));
        painter.drawEllipse(rect.center(), cell * 0.12, cell * 0.12);
    }
}

void ChessBoardWidget::drawPieces(QPainter& painter)
{
    for (int square = 0; square < 64; ++square) {
        if (isAnimatingSquare(square)) {
            continue;
        }
        if (m_dragging && square == m_dragFrom) {
            continue;
        }

        const Piece piece = m_position[square];
        if (!hasPiece(piece)) {
            continue;
        }

        const QRectF rect = squareRect(square);
        drawPiece(painter, piece, rect.adjusted(rect.width() * 0.08, rect.height() * 0.08,
                                               -rect.width() * 0.08, -rect.height() * 0.08));
    }

    if (m_animating && hasPiece(m_animationPiece)) {
        const QPointF from = squareCenter(m_animationFrom);
        const QPointF to = squareCenter(m_animationTo);
        const QPointF center = from + (to - from) * m_animationProgress;
        const qreal cell = squareSize();
        QRectF rect(center.x() - cell * 0.42, center.y() - cell * 0.42, cell * 0.84, cell * 0.84);
        drawPiece(painter, m_animationPiece, rect);
    }

    if (m_dragging && m_dragFrom >= 0 && hasPiece(m_position[m_dragFrom])) {
        const qreal cell = squareSize();
        QRectF rect(m_dragPos.x() - cell * 0.44, m_dragPos.y() - cell * 0.44, cell * 0.88, cell * 0.88);
        drawPiece(painter, m_position[m_dragFrom], rect, 0.94);
    }
}

void ChessBoardWidget::drawPiece(QPainter& painter, Piece piece, const QRectF& target, qreal opacity)
{
    const int index = static_cast<int>(piece);
    if (index < 0 || index >= static_cast<int>(m_piecePixmaps.size()) || m_piecePixmaps[index].isNull()) {
        return;
    }

    painter.save();
    painter.setOpacity(opacity * 0.28);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 150));
    painter.drawEllipse(target.adjusted(target.width() * 0.18, target.height() * 0.78,
                                        -target.width() * 0.18, target.height() * 0.03));

    painter.setOpacity(opacity);
    painter.drawPixmap(target, m_piecePixmaps[index], QRectF(m_piecePixmaps[index].rect()));
    painter.restore();
}

void ChessBoardWidget::drawCoordinates(QPainter& painter)
{
    const ThemeColors theme = colors();
    painter.save();
    painter.setPen(theme.coordinates);
    QFont font = painter.font();
    font.setPointSizeF(std::max<qreal>(8.0, squareSize() * 0.13));
    font.setWeight(QFont::DemiBold);
    painter.setFont(font);

    for (int square = 0; square < 64; ++square) {
        const int row = square / 8;
        const int col = square % 8;
        const QRectF rect = squareRect(square);

        const bool leftEdge = m_flipped ? col == 7 : col == 0;
        const bool bottomEdge = m_flipped ? row == 0 : row == 7;

        if (leftEdge) {
            painter.drawText(rect.adjusted(6, 4, -4, -4), Qt::AlignLeft | Qt::AlignTop, QString::number(8 - row));
        }
        if (bottomEdge) {
            const QChar file('a' + col);
            painter.drawText(rect.adjusted(4, 4, -7, -5), Qt::AlignRight | Qt::AlignBottom, QString(file));
        }
    }

    painter.restore();
}

ChessBoardWidget::ThemeColors ChessBoardWidget::colors() const
{
    switch (m_boardTheme) {
    case BoardTheme::Classic:
        return {
            QColor("#F0D9B5"),
            QColor("#E2C18F"),
            QColor("#B58863"),
            QColor("#946A47"),
            QColor(255, 255, 255, 42),
            QColor(62, 42, 28, 160)
        };
    case BoardTheme::Blue:
        return {
            QColor("#E8EEF7"),
            QColor("#CAD7EA"),
            QColor("#6F8EB8"),
            QColor("#4E6F99"),
            QColor(255, 255, 255, 38),
            QColor(22, 38, 62, 150)
        };
    case BoardTheme::Green:
        return {
            QColor("#E7F0DF"),
            QColor("#C8D9C3"),
            QColor("#6C9687"),
            QColor("#4F776F"),
            QColor(255, 255, 255, 42),
            QColor(18, 38, 38, 145)
        };
    case BoardTheme::Wood:
        return {
            QColor("#E4CBA6"),
            QColor("#CFA979"),
            QColor("#8A5F3E"),
            QColor("#6E472D"),
            QColor(255, 255, 255, 40),
            QColor(45, 30, 20, 150)
        };
    case BoardTheme::Modern:
    default:
        return {
            QColor("#DDE5E3"),
            QColor("#C2D0CC"),
            QColor("#455B68"),
            QColor("#34424D"),
            QColor(255, 255, 255, 36),
            QColor(18, 28, 36, 150)
        };
    }
}

bool ChessBoardWidget::isAnimatingSquare(int square) const
{
    return m_animating && (square == m_animationFrom || square == m_animationTo);
}
