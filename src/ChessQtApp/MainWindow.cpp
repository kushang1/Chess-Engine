#include "MainWindow.h"

#include <QDebug>
#include <QEvent>
#include <QGridLayout>
#include <QIcon>
#include <QSizePolicy>

#include <chrono>

namespace {

bool isWhitePiece(Piece piece)
{
    return piece >= WQ && piece <= WB;
}

QString iconPathForPiece(Piece piece)
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
    default: return "";
    }
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    ui.setupUi(this);

    QWidget* central = new QWidget(this);
    setCentralWidget(central);

    QHBoxLayout* mainLayout = new QHBoxLayout(central);

    QVBoxLayout* leftPanel = new QVBoxLayout();
    leftPanel->setContentsMargins(8, 8, 8, 8);
    leftPanel->setSpacing(6);

    QWidget* leftWidget = new QWidget();
    leftWidget->setLayout(leftPanel);
    leftWidget->setFixedWidth(150);
    mainLayout->addWidget(leftWidget);

    QHBoxLayout* buttonLayout = new QHBoxLayout();
    QPushButton* undoBtn = new QPushButton("Undo", this);
    QPushButton* redoBtn = new QPushButton("Redo", this);
    undoBtn->setFixedSize(50, 30);
    redoBtn->setFixedSize(50, 30);
    undoBtn->setStyleSheet("font-weight: bold; font-size: 14px;");
    redoBtn->setStyleSheet("font-weight: bold; font-size: 14px;");

    buttonLayout->addWidget(undoBtn);
    buttonLayout->addWidget(redoBtn);
    leftPanel->addLayout(buttonLayout);

    QLabel* historyLabel = new QLabel("Move History", this);
    historyLabel->setStyleSheet("font-weight: bold; font-size: 14px;");
    historyLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    leftPanel->addWidget(historyLabel);

    moveHistoryList = new QListWidget(this);
    moveHistoryList->setFixedWidth(134);
    moveHistoryList->setStyleSheet(
        "font-size: 13px; padding-left: 6px; padding-top: 4px; padding-bottom: 4px;"
    );
    moveHistoryList->setFrameShape(QFrame::StyledPanel);
    moveHistoryList->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

    QHBoxLayout* historyCenter = new QHBoxLayout();
    historyCenter->setContentsMargins(0, 0, 0, 0);
    historyCenter->addStretch();
    historyCenter->addWidget(moveHistoryList);
    historyCenter->addStretch();
    leftPanel->addLayout(historyCenter);
    leftPanel->addStretch();

    connect(undoBtn, &QPushButton::clicked, this, &MainWindow::undoMove);
    connect(redoBtn, &QPushButton::clicked, this, &MainWindow::redoMove);

    QVBoxLayout* centerPanel = new QVBoxLayout();
    mainLayout->addLayout(centerPanel);

    turnLabel = new QLabel("White's Turn", this);
    turnLabel->setAlignment(Qt::AlignCenter);
    turnLabel->setStyleSheet("font-size: 18px; font-weight: bold;");
    centerPanel->addWidget(turnLabel, 0, Qt::AlignHCenter);

    QWidget* boardWithLabels = new QWidget(this);
    QGridLayout* outerGrid = new QGridLayout(boardWithLabels);
    outerGrid->setSpacing(0);
    outerGrid->setContentsMargins(0, 0, 0, 0);

    for (int r = 0; r < 8; r++) {
        QLabel* rank = new QLabel(QString::number(8 - r));
        rank->setAlignment(Qt::AlignCenter);
        rank->setStyleSheet("font-size: 14px; font-weight: bold;");
        rank->setFixedSize(20, 80);
        outerGrid->addWidget(rank, r, 0);
    }

    for (int c = 0; c < 8; c++) {
        QLabel* file = new QLabel(QString(QChar('A' + c)));
        file->setAlignment(Qt::AlignCenter);
        file->setStyleSheet("font-size: 14px; font-weight: bold;");
        file->setFixedSize(80, 20);
        outerGrid->addWidget(file, 8, c + 1);
    }

    QWidget* boardWidget = new QWidget(this);
    boardWidget->setFixedSize(640, 640);

    QGridLayout* grid = new QGridLayout(boardWidget);
    grid->setSpacing(0);
    grid->setContentsMargins(0, 0, 0, 0);

    for (int row = 0; row < 8; ++row) {
        for (int col = 0; col < 8; ++col) {
            QPushButton* tile = new QPushButton(this);
            QString color = ((row + col) % 2 == 0) ? "#EEEED2" : "#769656";
            tile->setFixedSize(80, 80);
            tile->setIconSize(QSize(80, 80));
            tile->setStyleSheet(
                "background-color:" + color +
                "; border: none; padding: 0px; margin: 0px;"
            );
            grid->addWidget(tile, row, col);
            boardButtons[row][col] = tile;

            connect(tile, &QPushButton::clicked, this, [=]() {
                selectedSquare = row * 8 + col;
                handleTileClick();
                });
        }
    }

    outerGrid->addWidget(boardWidget, 0, 1, 8, 8);
    centerPanel->addWidget(boardWithLabels, 0, Qt::AlignHCenter);
    centerPanel->addStretch();

    QVBoxLayout* rightPanel = new QVBoxLayout();
    QWidget* rightWidget = new QWidget();
    rightWidget->setLayout(rightPanel);
    rightWidget->setFixedWidth(120);
    mainLayout->addWidget(rightWidget);

    QPushButton* pushie = new QPushButton("Calculate Moves", this);
    pushie->setFixedSize(50, 30);
    pushie->setStyleSheet("font-weight: bold; font-size: 14px;");
    rightPanel->addWidget(pushie);
    connect(pushie, &QPushButton::clicked, this, &MainWindow::CalculateMoves);

    engine.newGame();
    updateBoardUI();

    positionHistory.clear();
    repetitionHistory.clear();
    moveHistory.clear();
    recordCurrentPosition();
    currentMoveIndex = 0;
    moveHistoryList->clear();

    connect(moveHistoryList, &QListWidget::currentRowChanged,
        this, &MainWindow::onHistoryItemSelected);

    QShortcut* undoShortcut = new QShortcut(QKeySequence("Ctrl+Z"), this);
    connect(undoShortcut, &QShortcut::activated, this, &MainWindow::undoMove);

    QShortcut* redoShortcut = new QShortcut(QKeySequence("Ctrl+Y"), this);
    connect(redoShortcut, &QShortcut::activated, this, &MainWindow::redoMove);
}

MainWindow::~MainWindow() = default;

void MainWindow::handleTileClick()
{
    static int fromRow = -1;
    static int fromCol = -1;

    if (!pieceSelected) {
        Piece piece = engine.pieceAt(selectedSquare);
        if (piece == EMPTY) {
            return;
        }

        if (engine.isWhiteTurn() != isWhitePiece(piece)) {
            return;
        }

        pieceSelected = true;
        fromRow = selectedSquare / 8;
        fromCol = selectedSquare % 8;

        boardButtons[fromRow][fromCol]->setStyleSheet("background-color: yellow; border: none;");

        std::vector<Move> moves = engine.legalMoves();
        std::vector<Move> fromSquareMoves;
        for (const Move& move : moves) {
            if (move.from == selectedSquare) {
                fromSquareMoves.push_back(move);
            }
        }
        highlightMoves(fromSquareMoves);
        return;
    }

    bool valid = false;
    Move selectedMove;
    clearHighlights();

    int fromSquare = fromRow * 8 + fromCol;
    std::vector<Move> legalMoves = engine.legalMoves();
    for (const Move& move : legalMoves) {
        if (move.from == fromSquare && move.to == selectedSquare) {
            selectedMove = move;
            valid = true;
            break;
        }
    }

    resetColors();

    if (!valid) {
        pieceSelected = false;
        fromRow = fromCol = -1;
        return;
    }

    truncateHistory();

    bool humanWasWhite = engine.isWhiteTurn();
    QString humanSan = QString::fromStdString(engine.moveToSan(selectedMove));
    if (!engine.makeMove(selectedMove)) {
        pieceSelected = false;
        fromRow = fromCol = -1;
        return;
    }

    updateBoardUI();
    lastFrom = selectedMove.from;
    lastTo = selectedMove.to;
    highlightLastMove();

    moveHistory.push_back(selectedMove);
    recordCurrentPosition();
    currentMoveIndex = static_cast<int>(positionHistory.size()) - 1;
    addMoveToHistory(humanWasWhite, humanSan);
    moveHistoryList->setCurrentRow((currentMoveIndex - 1) / 2);

    if (checkDrawByRepetitionOr50() || updateGameStatusLabel()) {
        pieceSelected = false;
        fromRow = fromCol = -1;
        return;
    }

    qDebug() << "Engine is thinking...";
    setEnabled(false);
    auto start = std::chrono::high_resolution_clock::now();
    std::vector<uint64_t> repetitions = repetitionHistory;

    QFuture<chess::SearchResult> future = QtConcurrent::run(
        [this, repetitions]() {
            chess::SearchLimits limits;
            limits.maxDepth = 128;
            limits.moveTimeMs = 1000;
            engine.clearSearchStop();
            return engine.findBestMove(limits, repetitions);
        });

    QFutureWatcher<chess::SearchResult>* watcher = new QFutureWatcher<chess::SearchResult>(this);

    connect(watcher, &QFutureWatcher<chess::SearchResult>::finished, this, [=]() {
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = end - start;
        qDebug() << "Engine move found in" << elapsed.count() << "seconds.";

        chess::SearchResult result = watcher->future().result();
        Move engineMove = result.bestMove;

        if (engineMove.from == -1) {
            turnLabel->setText("Engine lost");
            pieceSelected = false;
            fromRow = fromCol = -1;
            setEnabled(true);
            watcher->deleteLater();
            return;
        }

        bool engineWasWhite = engine.isWhiteTurn();
        QString engineSan = QString::fromStdString(engine.moveToSan(engineMove));
        if (!engine.makeMove(engineMove)) {
            turnLabel->setText("Engine returned illegal move");
            pieceSelected = false;
            fromRow = fromCol = -1;
            setEnabled(true);
            watcher->deleteLater();
            return;
        }

        updateBoardUI();
        lastFrom = engineMove.from;
        lastTo = engineMove.to;
        highlightLastMove();

        moveHistory.push_back(engineMove);
        recordCurrentPosition();
        currentMoveIndex = static_cast<int>(positionHistory.size()) - 1;
        addMoveToHistory(engineWasWhite, engineSan);
        moveHistoryList->setCurrentRow((currentMoveIndex - 1) / 2);

        if (!checkDrawByRepetitionOr50()) {
            updateGameStatusLabel();
        }

        setEnabled(true);
        watcher->deleteLater();
        });

    watcher->setFuture(future);
    turnLabel->setText("Engine is thinking...");

    pieceSelected = false;
    fromRow = fromCol = -1;
}

void MainWindow::undoMove()
{
    if (currentMoveIndex == 0) {
        return;
    }

    restorePosition(currentMoveIndex - 1);

    int moveNum = currentMoveIndex / 2;
    moveHistoryList->setCurrentRow(moveNum - 1);
}

void MainWindow::redoMove()
{
    if (currentMoveIndex + 1 >= static_cast<int>(positionHistory.size())) {
        return;
    }

    restorePosition(currentMoveIndex + 1);

    int moveNum = currentMoveIndex / 2;
    moveHistoryList->setCurrentRow(moveNum - 1);
}

void MainWindow::updateBoardUI()
{
    for (int row = 0; row < 8; ++row) {
        for (int col = 0; col < 8; ++col) {
            int square = row * 8 + col;
            QString iconPath = iconPathForPiece(engine.pieceAt(square));

            if (!iconPath.isEmpty()) {
                QIcon icon(iconPath);
                boardButtons[row][col]->setIcon(icon);
                boardButtons[row][col]->setIconSize(boardButtons[row][col]->size());
            }
            else {
                boardButtons[row][col]->setIcon(QIcon());
            }
        }
    }

    turnLabel->setText(engine.isWhiteTurn() ? "White's Turn" : "Black's Turn");
}

void MainWindow::resetColors()
{
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            QString color = ((r + c) % 2 == 0) ? "#EEEED2" : "#769656";
            boardButtons[r][c]->setStyleSheet("background-color:" + color + "; border: none;");
        }
    }
}

void MainWindow::highlightMoves(const std::vector<Move>& moves)
{
    clearHighlights();

    for (const Move& move : moves) {
        int square = move.to;
        int r = square / 8;
        int c = square % 8;
        QWidget* overlay = new QWidget(boardButtons[r][c]);
        overlay->setAttribute(Qt::WA_TransparentForMouseEvents);
        overlay->setGeometry(0, 0, 80, 80);

        bool isCapture = engine.pieceAt(square) != EMPTY || move.wasEnPassant;
        if (!isCapture) {
            QWidget* dot = new QWidget(overlay);
            dot->setAttribute(Qt::WA_TransparentForMouseEvents);
            dot->setFixedSize(22, 22);
            dot->move((80 - 22) / 2, (80 - 22) / 2);
            dot->setStyleSheet(
                "background-color: rgba(0,0,0,50);"
                "border-radius: 11px;"
                "border: none;"
            );
            dot->show();
        }
        else {
            overlay->setStyleSheet("background-color: transparent;");
            overlay->setFixedSize(80, 80);
            overlay->move(0, 0);
            overlay->installEventFilter(this);
        }

        overlay->show();
        highlightOverlays.push_back(overlay);
    }
}

void MainWindow::clearHighlights()
{
    for (QWidget* widget : highlightOverlays) {
        widget->deleteLater();
    }
    highlightOverlays.clear();
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::Paint) {
        QWidget* widget = qobject_cast<QWidget*>(obj);
        if (!widget) {
            return false;
        }

        QPainter painter(widget);
        painter.setRenderHint(QPainter::Antialiasing, true);

        QPen pen(QColor(0, 0, 0, 50));
        pen.setWidth(6);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QPoint(widget->width() / 2, widget->height() / 2), 35, 35);
    }
    return false;
}

void MainWindow::CalculateMoves()
{
    qDebug() << "Starting Perft Test";

    int depth = 5;
    auto start = std::chrono::high_resolution_clock::now();
    chess::PerftResult result = engine.perft(depth);
    auto end = std::chrono::high_resolution_clock::now();

    std::chrono::duration<double> elapsed = end - start;
    qDebug() << "Perft(" << depth << ") nodes:" << result.nodes;
    qDebug() << "Time:" << elapsed.count() << "seconds";
}

void MainWindow::onHistoryItemSelected(int row)
{
    if (row < 0) {
        return;
    }

    int posIndex = (row + 1) * 2;
    if (posIndex > static_cast<int>(positionHistory.size()) - 1) {
        posIndex = static_cast<int>(positionHistory.size()) - 1;
    }

    restorePosition(posIndex);
}

void MainWindow::recordCurrentPosition()
{
    positionHistory.push_back(engine.currentFen());
    repetitionHistory.push_back(engine.positionHash());
}

void MainWindow::restorePosition(int positionIndex)
{
    if (positionIndex < 0 || positionIndex >= static_cast<int>(positionHistory.size())) {
        return;
    }

    currentMoveIndex = positionIndex;
    engine.setPositionFromFen(positionHistory[currentMoveIndex]);
    updateBoardUI();

    if (currentMoveIndex > 0 && currentMoveIndex - 1 < static_cast<int>(moveHistory.size())) {
        lastFrom = moveHistory[currentMoveIndex - 1].from;
        lastTo = moveHistory[currentMoveIndex - 1].to;
        highlightLastMove();
    }
    else {
        lastFrom = -1;
        lastTo = -1;
        resetColors();
    }

    updateGameStatusLabel();
}

void MainWindow::truncateHistory()
{
    if (currentMoveIndex + 1 >= static_cast<int>(positionHistory.size())) {
        return;
    }

    positionHistory.resize(currentMoveIndex + 1);
    repetitionHistory.resize(currentMoveIndex + 1);
    moveHistory.resize(currentMoveIndex);

    while (moveHistoryList->count() > (currentMoveIndex + 1) / 2) {
        delete moveHistoryList->takeItem(moveHistoryList->count() - 1);
    }
}

void MainWindow::addMoveToHistory(bool whiteMove, const QString& san)
{
    if (whiteMove) {
        int moveNumber = static_cast<int>(moveHistory.size() / 2) + 1;
        QString row = QString("%1. %2").arg(moveNumber).arg(san);
        moveHistoryList->addItem(row);
        moveHistoryList->setCurrentRow(moveHistoryList->count() - 1);
        return;
    }

    int rowIndex = moveHistoryList->count() - 1;
    if (rowIndex >= 0) {
        QString existing = moveHistoryList->item(rowIndex)->text();
        existing += " " + san;
        moveHistoryList->item(rowIndex)->setText(existing);
        moveHistoryList->setCurrentRow(rowIndex);
    }
}

void MainWindow::highlightLastMove()
{
    resetColors();

    if (lastFrom == -1 || lastTo == -1) {
        return;
    }

    auto highlightColor = [](int r, int c) {
        bool isLight = ((r + c) % 2 == 0);
        return isLight ? "#f7f683" : "#baca44";
    };

    int fr = lastFrom / 8;
    int fc = lastFrom % 8;
    boardButtons[fr][fc]->setStyleSheet(
        "background-color: " + QString(highlightColor(fr, fc)) + "; border: none;");

    int tr = lastTo / 8;
    int tc = lastTo % 8;
    boardButtons[tr][tc]->setStyleSheet(
        "background-color: " + QString(highlightColor(tr, tc)) + "; border: none;");
}

bool MainWindow::checkDrawByRepetitionOr50()
{
    uint64_t currentHash = engine.positionHash();

    int count = 0;
    for (uint64_t hash : repetitionHistory) {
        if (hash == currentHash) {
            ++count;
        }
    }

    if (count >= 3) {
        turnLabel->setText("Draw by Threefold Repetition");
        return true;
    }

    if (engine.gameStatus().kind == chess::GameStatusKind::FiftyMoveRule) {
        turnLabel->setText("Draw by 50-move Rule");
        return true;
    }

    return false;
}

bool MainWindow::updateGameStatusLabel()
{
    chess::GameStatus status = engine.gameStatus();

    switch (status.kind) {
    case chess::GameStatusKind::Checkmate:
        turnLabel->setText(status.whiteToMove ? "White is checkmated" : "Black is checkmated");
        return true;
    case chess::GameStatusKind::Stalemate:
        turnLabel->setText("Draw by stalemate");
        return true;
    case chess::GameStatusKind::FiftyMoveRule:
        turnLabel->setText("Draw by 50-move Rule");
        return true;
    case chess::GameStatusKind::Ongoing:
    default:
        turnLabel->setText(engine.isWhiteTurn() ? "White's Turn" : "Black's Turn");
        return false;
    }
}
