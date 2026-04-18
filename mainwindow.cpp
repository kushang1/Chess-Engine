#include "mainwindow.h"
#include "ZobristHashing.h"
#include "polyglot.h"
static ZobristHashing g_zobrist;


MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    this->engine.moveGenerator = &(this->moveGenerator);
    ui.setupUi(this);

    QWidget* central = new QWidget(this);
    setCentralWidget(central);

    // --- Main horizontal layout ---
    QHBoxLayout* mainLayout = new QHBoxLayout(central);

    // ================================================================
    // LEFT PANEL
    // ================================================================
    QVBoxLayout* leftPanel = new QVBoxLayout();

    // Give the left panel inner padding so children are not flush to the edge
    leftPanel->setContentsMargins(8, 8, 8, 8);
    leftPanel->setSpacing(6);

    QWidget* leftWidget = new QWidget();
    leftWidget->setLayout(leftPanel);
    leftWidget->setFixedWidth(150);
    mainLayout->addWidget(leftWidget);

    // Undo/Redo buttons
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    QPushButton* undoBtn = new QPushButton("Undo", this);
    QPushButton* redoBtn = new QPushButton("Redo", this);
    undoBtn->setFixedSize(50, 30);
    redoBtn->setFixedSize(50, 30);
    undoBtn->setStyleSheet("font-weight: bold; font-size: 14px;");
    redoBtn->setStyleSheet("font-weight: bold; font-size: 14px;");

    buttonLayout->addWidget(undoBtn);
    buttonLayout->addWidget(redoBtn);

    // Put the buttons at the top-left of the left panel (keep them left aligned)
    leftPanel->addLayout(buttonLayout);

    // Move history label (left aligned inside left panel)
    QLabel* historyLabel = new QLabel("Move History", this);
    historyLabel->setStyleSheet("font-weight: bold; font-size: 14px;");
    historyLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    leftPanel->addWidget(historyLabel);

    // Move history list - give it a clear frame and a fixed width that fits inside left panel margins
    moveHistoryList = new QListWidget(this);
    moveHistoryList->setFixedWidth(134); // 120 - left/right margins (8+8) = 104
    moveHistoryList->setStyleSheet(
        "font-size: 13px; padding-left: 6px; padding-top: 4px; padding-bottom: 4px;"
    );
    moveHistoryList->setFrameShape(QFrame::StyledPanel);
    moveHistoryList->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

    // Center the list horizontally inside the left panel but leave it touching top after label
    QHBoxLayout* historyCenter = new QHBoxLayout();
    historyCenter->setContentsMargins(0, 0, 0, 0);
    historyCenter->addStretch();
    historyCenter->addWidget(moveHistoryList);
    historyCenter->addStretch();
    leftPanel->addLayout(historyCenter);

    leftPanel->addStretch();

    connect(undoBtn, &QPushButton::clicked, this, &MainWindow::undoMove);
    connect(redoBtn, &QPushButton::clicked, this, &MainWindow::redoMove);

    // ================================================================
    // CENTER PANEL (turn label + board with rank/file labels)
    // ================================================================
    QVBoxLayout* centerPanel = new QVBoxLayout();
    mainLayout->addLayout(centerPanel);

    // Turn label
    turnLabel = new QLabel("White's Turn", this);
    turnLabel->setAlignment(Qt::AlignCenter);
    turnLabel->setStyleSheet("font-size: 18px; font-weight: bold;");
    centerPanel->addWidget(turnLabel, 0, Qt::AlignHCenter);

    // Outer wrapper for board + labels
    QWidget* boardWithLabels = new QWidget(this);
    QGridLayout* outerGrid = new QGridLayout(boardWithLabels);
    outerGrid->setSpacing(0);
    outerGrid->setContentsMargins(0, 0, 0, 0);

    // --- Rank labels (1–8) on left ---
    for (int r = 0; r < 8; r++) {
        QLabel* rank = new QLabel(QString::number(8 - r));
        rank->setAlignment(Qt::AlignCenter);
        rank->setStyleSheet("font-size: 14px; font-weight: bold;");
        rank->setFixedSize(20, 80);
        outerGrid->addWidget(rank, r, 0);
    }

    // --- File labels (A–H) on bottom ---
    for (int c = 0; c < 8; c++) {
        QLabel* file = new QLabel(QString(QChar('A' + c)));
        file->setAlignment(Qt::AlignCenter);
        file->setStyleSheet("font-size: 14px; font-weight: bold;");
        file->setFixedSize(80, 20);
        outerGrid->addWidget(file, 8, c + 1);
    }

    // --- Actual chessboard ---
    QWidget* boardWidget = new QWidget(this);
    boardWidget->setFixedSize(640, 640);

    QGridLayout* grid = new QGridLayout(boardWidget);
    grid->setSpacing(0);
    grid->setContentsMargins(0, 0, 0, 0);

    const int SIZE = 8;
    for (int row = 0; row < SIZE; ++row) {
        for (int col = 0; col < SIZE; ++col) {

            QPushButton* tile = new QPushButton(this);
            QString color = ((row + col) % 2 == 0) ? "#EEEED2" : "#769656";
            tile->setFixedSize(80, 80);

            // FULL square chess-piece behavior (chess.com-like)
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

    // Add board inside outer grid (row 0–7, col 1–8)
    outerGrid->addWidget(boardWidget, 0, 1, 8, 8);

    centerPanel->addWidget(boardWithLabels, 0, Qt::AlignHCenter);
    centerPanel->addStretch();

    // ================================================================
    // RIGHT PANEL
    // ================================================================
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

    // ================================================================
    // Init game
     //================================================================
    gameBoard.resetBoard();
    //const std::string fen = "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8";
    //const std::string fen = "r1bq1rk1/ppp1bppp/2np1n2/3Np3/2B1P3/2N1BP2/PPPQ2PP/2KR3R w - - 0 10";
	//const std::string fen = "7r/8/kr6/8/4K3/8/8/2R6 w - - 0 10";/* rook endgame*/
	//const std::string fen = "8/8/8/4p3/4k3/8/4K3/8 w - - 0 0"; /*pawn endgame*/
	//const std::string fen = "4q1R1/7K/5k2/8/8/8/8/8 w - - 0 0"; /*rook vs queen endgame*/
	//const std::string fen = "8/8/8/4k3/4p3/4K3/8/8 w - - 0 0"; /*rook vs queen endgame*/
    //const std::string fen = "1r4rk/5p1p/5R2/4B3/8/8/7P/7K b - - 0 1"; mate in 3
    //const std::string fen = "8/7r/6p1/6p1/5pPk/5P1p/5P1K/R7 b - - 0 1"; mate in 3
    //const std::string fen = "r5rk/5p1p/5R2/4B3/8/8/7P/7K w - - 0 1"; mate in 3
    //const std::string fen = "1r3k1r/ppp2ppp/3p1b2/q7/2P1Q3/1P3NP1/P4PP1/1R2R1K1 w - - 0 1";
    //const std::string fen = "1rb1r1k1/1qp3pp/R3p3/4Pp2/2B2Q2/4B2P/1b3PPK/4R3 w - - 0 1";
    //const std::string fen = "6kr/1R6/3p2p1/3P2Pp/3b1P2/6K1/8/8 w - - 0 1";
    //const std::string fen = "6kr/3R4/3p2p1/2bP2Pp/41P2/6K1/8/8 w - - 0 1";
    //const std::string fen = "r1bq1rk1/pp2bppp/2n1pn2/2pp4/3P1B2/2P1PN2/PP1NBPPP/R2Q1RK1 w - - 6 10";
    //const std::string fen = "8/2p1k3/4pp2/3p3B/3P3P/4rbK1/6P1/8 w - - 0 1";
    //const std::string fen = "8/kp6/p5q1/4p2p/8/8/5K2/8 w - - 0 1"; 
    //const std::string fen = "rnbqkbnr/ppp1pppp/8/3p4/3P4/8/PPP1PPPP/RNBQKBNR w KQkq d6 0 2"; 
    //const std::string fen = "rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 2"; 
    //const std::string fen = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 2"; 
    //const std::string fen = "rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR b KQkq - 0 2"; 
    //const std::string fen = "rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3";
    //const std::string fen = "r1bqkb1r/ppp2ppp/2n1pn2/3p4/3P4/4PN2/PPP1BPPP/RNBQK2R w KQkq - 0 6";
    //const std::string fen = "7k/8/8/2p1p2p/2P1p2P/4P3/8/7K w - - 0 6";
    //const std::string fen = "8/1R1P2bk/6p1/6P1/4KP2/r7/7p/8 w - - 0 6";
    //gameBoard.loadFEN(fen);
    

//#include  "polyglot.h"
//    
//    const std::string fen1 = "rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPPKPPP/RNBQ1BNR b kq - 0 1"; 
//    gameBoard.loadFEN(fen1);
//    auto x = polyglotHash(gameBoard);
//    const std::string fen2 = "rnbq1bnr/ppp1pkpp/8/3pPp2/8/8/PPPPKPPP/RNBQ1BNR w - - 0 4"; 
//    gameBoard.loadFEN(fen2);
//    x = polyglotHash(gameBoard);
//    const std::string fen3 = "rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR b KQkq - 0 2"; 
//    gameBoard.loadFEN(fen3);
//    x = polyglotHash(gameBoard);
//    const std::string fen4 = "rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3";
//    gameBoard.loadFEN(fen4);
//    x = polyglotHash(gameBoard);
    //gameBoard.loadFEN(fen);
//#include  "polyglot.h"
    //auto x = polyglotHash(gameBoard);
    updateBoardUI();

    positionHistory.clear();
    moveHistory.clear();
    positionHistory.push_back(gameBoard);   // initial position
    currentMoveIndex = 0;
    moveHistoryList->clear();

    // --- NEW: clicking move in history jumps to that move ---
    connect(moveHistoryList, &QListWidget::currentRowChanged,
        this, &MainWindow::onHistoryItemSelected);

    // Keyboard shortcuts
    QShortcut* undoShortcut = new QShortcut(QKeySequence("Ctrl+Z"), this);
    connect(undoShortcut, &QShortcut::activated, this, &MainWindow::undoMove);

    QShortcut* redoShortcut = new QShortcut(QKeySequence("Ctrl+Y"), this);
    connect(redoShortcut, &QShortcut::activated, this, &MainWindow::redoMove);

}

MainWindow::~MainWindow()
{
}

void MainWindow::handleTileClick() {

    static int fromRow = -1, fromCol = -1;

    if (!pieceSelected) {

        uint64_t hash = polyglotHash(gameBoard);

        Piece piece = gameBoard.pieceAt(selectedSquare);
        if (piece == EMPTY) return;

        bool isWhitePiece = (piece >= WQ && piece <= WB);

        if ((gameBoard.isWhiteTurn && !isWhitePiece) || (!gameBoard.isWhiteTurn && isWhitePiece))
            return;

        pieceSelected = true;
        fromRow = selectedSquare / 8;
        fromCol = selectedSquare % 8;

        boardButtons[fromRow][fromCol]->setStyleSheet("background-color: yellow; border: none;");

        auto moves = moveGenerator.generateLegalMoves(gameBoard);
        std::vector<Move> mv;
        for (auto i : moves) {
            if (i.from == selectedSquare) {
                mv.push_back(i);
            }
        }
        highlightMoves(mv);

        return;
    }

    if (pieceSelected) {
        bool valid = false;
        Move mv;
        clearHighlights();
        auto legalMoves = moveGenerator.generateLegalMoves(gameBoard);
        for (auto& i : legalMoves) {
            if (i.to == selectedSquare && i.from == gameBoard.toIndex(fromRow, fromCol)) {
                mv = i;
                valid = true;
                break;
            }
        }

        resetColors();

        if (valid) {
            // --- HUMAN MOVE ---
            truncateHistory();  // in case we undid and now branch a new line

            board before = gameBoard;       // snapshot before move (for notation)
            gameBoard.makeMove(mv);
            updateBoardUI();

            lastFrom = mv.from;
            lastTo = mv.to;
            highlightLastMove();

            // update history
            positionHistory.push_back(gameBoard);
            moveHistory.push_back(mv);
            currentMoveIndex = (int)positionHistory.size() - 1;

            // add to move list
            board afterHuman = gameBoard;   // after applying move
            addMoveToHistory(mv, before, afterHuman);

            moveHistoryList->setCurrentRow(currentMoveIndex - 1);

            // --- DRAW CHECK AFTER HUMAN MOVE ---
            if (checkDrawByRepetitionOr50()) {
                pieceSelected = false;
                fromRow = fromCol = -1;
                return;   // stop engine
            }
        }

        if (valid) {
            // --- Check if game already over (engine side has no moves) ---
            auto engineMoves = moveGenerator.generateLegalMoves(gameBoard);
            if (engineMoves.empty()) {
                bool whiteToMove = gameBoard.isWhiteTurn;
                int kingSq = moveGenerator.findKing(gameBoard, whiteToMove);
                bool inCheck = moveGenerator.isSquareAttacked(gameBoard, kingSq, !whiteToMove);

                if (inCheck) {
                    // side to move is checkmated
                    if (whiteToMove)
                        turnLabel->setText("White is checkmated");
                    else
                        turnLabel->setText("Black is checkmated");
                }
                else {
                    turnLabel->setText("Draw by stalemate");
                }

                pieceSelected = false;
                fromRow = fromCol = -1;
                return;
            }

            // --- ENGINE MOVE (async) ---
            QDebug debug = qDebug();
            debug << "Engine is thinking...";

            setEnabled(false);
            auto start = std::chrono::high_resolution_clock::now();

            // copy board for engine (safer)
            board engineBoard = gameBoard;

            std::vector<uint64_t> globalReps;
            globalReps.reserve(positionHistory.size());

            for (auto& pos : positionHistory)
                globalReps.push_back(g_zobrist.computeHash(pos));

            QFuture<Move> future = QtConcurrent::run(
                [this, engineBoard, depth = 128, globalReps]() mutable {
                    return engine.findBestMove(engineBoard, depth, globalReps);
                });

            QFutureWatcher<Move>* watcher = new QFutureWatcher<Move>(this);

            connect(watcher, &QFutureWatcher<Move>::finished, this, [=]() {
                auto end = std::chrono::high_resolution_clock::now();
                std::chrono::duration<double> elapsed = end - start;
                qDebug() << "Engine move found in" << elapsed.count() << "seconds.";

                Move engineMove = watcher->future().result();

                if (engineMove.from == -1) {
                    turnLabel->setText("Engine lost");
                    pieceSelected = false;
                    fromRow = fromCol = -1;
                    setEnabled(true);
                    watcher->deleteLater();
                    return;
                }

                // --- Apply engine move to real gameBoard ---
                board before = gameBoard;  // snapshot pre-move
                gameBoard.makeMove(engineMove);
                updateBoardUI();

                lastFrom = engineMove.from;
                lastTo = engineMove.to;
                highlightLastMove();

                // update history
                positionHistory.push_back(gameBoard);
                moveHistory.push_back(engineMove);
                currentMoveIndex = (int)positionHistory.size() - 1;

                board afterEngine = gameBoard;
                addMoveToHistory(engineMove, before, afterEngine);

                moveHistoryList->setCurrentRow(currentMoveIndex - 1);

                // --- DRAW CHECK AFTER ENGINE MOVE ---
                if (checkDrawByRepetitionOr50()) {
                    setEnabled(true);
                    pieceSelected = false;
                    fromRow = fromCol = -1;
                    watcher->deleteLater();
                    return;
                }

                // --- Check game over after engine move ---
                auto humanMoves = moveGenerator.generateLegalMoves(gameBoard);
                if (humanMoves.empty()) {
                    bool whiteToMove = gameBoard.isWhiteTurn;
                    int kingSq = moveGenerator.findKing(gameBoard, whiteToMove);
                    bool inCheck = moveGenerator.isSquareAttacked(gameBoard, kingSq, !whiteToMove);

                    if (inCheck) {
                        if (whiteToMove)
                            turnLabel->setText("White is checkmated");
                        else
                            turnLabel->setText("Black is checkmated");
                    }
                    else {
                        turnLabel->setText("Draw by stalemate");
                    }
                }
                else {
                    turnLabel->setText(gameBoard.isWhiteTurn ? "White's Turn" : "Black's Turn");
                }

                setEnabled(true);
                watcher->deleteLater();
                });

            watcher->setFuture(future);
            turnLabel->setText("Engine is thinking...");
        }

        pieceSelected = false;
        fromRow = fromCol = -1;
    }
}


void MainWindow::undoMove() {
    if (currentMoveIndex == 0)
        return;

    currentMoveIndex--;
    gameBoard = positionHistory[currentMoveIndex];
    updateBoardUI();

    int moveNum = currentMoveIndex / 2;   // each 2 plies = 1 row
    moveHistoryList->setCurrentRow(moveNum - 1);
}


void MainWindow::redoMove() {
    if (currentMoveIndex + 1 >= (int)positionHistory.size())
        return;

    currentMoveIndex++;
    gameBoard = positionHistory[currentMoveIndex];
    updateBoardUI();

    int moveNum = currentMoveIndex / 2;
    moveHistoryList->setCurrentRow(moveNum - 1);
}



void MainWindow::updateBoardUI() {

    for (int row = 0; row < 8; ++row) {
        for (int col = 0; col < 8; ++col) {
            int ind = gameBoard.toIndex(row, col);
            Piece piece = gameBoard.pieceAt(ind);
            QString iconPath;

            switch (piece) {
            case WP: iconPath = "images/white_pawn.png"; break;
            case WR: iconPath = "images/white_rook.png"; break;
            case WN: iconPath = "images/white_knight.png"; break;
            case WB: iconPath = "images/white_bishop.png"; break;
            case WQ: iconPath = "images/white_queen.png"; break;
            case WK: iconPath = "images/white_king.png"; break;

            case BP: iconPath = "images/black_pawn.png"; break;
            case BR: iconPath = "images/black_rook.png"; break;
            case BN: iconPath = "images/black_knight.png"; break;
            case BB: iconPath = "images/black_bishop.png"; break;
            case BQ: iconPath = "images/black_queen.png"; break;
            case BK: iconPath = "images/black_king.png"; break;

            default: iconPath = ""; break;
            }

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
    turnLabel->setText(gameBoard.isWhiteTurn ? "White's Turn" : "Black's Turn");
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


void MainWindow::highlightMoves(std::vector<Move>& moves) {
    clearHighlights();

    for (auto& mv : moves) {
        int square = mv.to;
        int r = square / 8;
        int c = square % 8;
        QWidget* overlay = new QWidget(boardButtons[r][c]);
        overlay->setAttribute(Qt::WA_TransparentForMouseEvents);
        overlay->setGeometry(0, 0, 80, 80);

        bool isCapture = (gameBoard.pieceAt(square) != EMPTY);

        if (!isCapture) {

            QWidget* dot = new QWidget(overlay);
            dot->setAttribute(Qt::WA_TransparentForMouseEvents);

            dot->setFixedSize(22, 22);                 // perfect dot size
            dot->move((80 - 22) / 2, (80 - 22) / 2);   // perfectly centered

            dot->setStyleSheet(
                "background-color: rgba(0,0,0,50);"     // translucent black
                "border-radius: 11px;"                  // circle
                "border: none;"
            );

            dot->show();
        }
        else {
            // ---- Capture Move: Even translucent ring ----
            overlay->setStyleSheet("background-color: transparent;");
            overlay->setFixedSize(80, 80);
            overlay->move(0, 0);

            // Custom paint event for clean ring
            overlay->installEventFilter(this);
        }

        overlay->show();
        highlightOverlays.push_back(overlay);
    }
}

void MainWindow::clearHighlights() {
    for (auto* w : highlightOverlays) {
        w->deleteLater();
    }
    highlightOverlays.clear();
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::Paint) {
        QWidget* w = qobject_cast<QWidget*>(obj);
        if (!w) return false;

        QPainter p(w);
        p.setRenderHint(QPainter::Antialiasing, true);

        int outerRadius = 70;
        int innerRadius = 58;

        QPoint center(w->width() / 2, w->height() / 2);

        // Outer circle (translucent)
        QPen pen(QColor(0, 0, 0, 50));
        pen.setWidth(6);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);

        // Draw smooth ring
        p.drawEllipse(center, outerRadius / 2, outerRadius / 2);
    }
    return false;
}

void MainWindow::CalculateMoves() {
    qDebug() << "Starting Perft Test";

    int depth = 5;  // change as needed
    board b = gameBoard;

    auto start = std::chrono::high_resolution_clock::now();

    long long nodes = perft(depth, b);

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;

    qDebug() << "Perft(" << depth << ") nodes:" << nodes;
    qDebug() << "Time:" << elapsed.count() << "seconds";
}

long long MainWindow::perft(int depth, board& b) {
    if (depth == 0) {
        return 1;
    }

    long long nodes = 0;
    MoveList moves;
    moveGenerator.generateLegalMoves(b, moves);

    for (Move& m : moves) {
        Unmove u = b.makeMove(m);

        if (depth == 1) {
            ++nodes;
        }
        else {
            nodes += perft(depth - 1, b);
        }

        b.unmakeMove(m, u); 
    }

    return nodes;
}

QString MainWindow::squareToString(int sq) const {
    int row = sq / 8;
    int col = sq % 8;
    QChar file('a' + col);
    QChar rank('8' - row);
    return QString("%1%2").arg(file).arg(rank);
}

QString MainWindow::moveToString(const Move& m, const board& before) const {
    QString s = squareToString(m.from) + " → " + squareToString(m.to);
    bool whiteMoved = before.isWhiteTurn;      // side to move *before* move
    return (whiteMoved ? "W: " : "B: ") + s;
}

void MainWindow::onHistoryItemSelected(int row)
{
    if (row < 0)
        return;

    // row = move number index
    int posIndex = (row + 1) * 2;   // each row = 2 plies (white + black)

    if (posIndex > (int)positionHistory.size() - 1)
        posIndex = positionHistory.size() - 1;

    currentMoveIndex = posIndex;
    gameBoard = positionHistory[currentMoveIndex];
    updateBoardUI();
}


QString MainWindow::toSAN(const Move& m, board& before, board& after)
{
    MoveGenerator mg;

    bool whiteToMove = before.isWhiteTurn;
    Piece moved = m.moved;
    Piece captured = m.captured;

    QString san;

    // ----------------------------------------------------
    // 1. CASTLING
    // ----------------------------------------------------
    if (m.wasCastling) {
        if (whiteToMove) {
            return (m.to == 62) ? "O-O" : "O-O-O";
        }
        else {
            return (m.to == 6) ? "O-O" : "O-O-O";
        }
    }

    // ----------------------------------------------------
    // 2. PIECE LETTER (empty for pawns)
    // ----------------------------------------------------
    QChar pieceChar = '\0';
    switch (moved) {
    case WN: case BN: pieceChar = 'N'; break;
    case WB: case BB: pieceChar = 'B'; break;
    case WR: case BR: pieceChar = 'R'; break;
    case WQ: case BQ: pieceChar = 'Q'; break;
    case WK: case BK: pieceChar = 'K'; break;
    default: pieceChar = '\0'; break;   // pawn
    }

    if (pieceChar != '\0')
        san.append(pieceChar);

    // ----------------------------------------------------
    // 3. Pawn capture notation (exd5)
    // ----------------------------------------------------
    if (pieceChar == '\0' && (captured != EMPTY || m.wasEnPassant)) {
        int fromCol = m.from % 8;
        san.append(QChar('a' + fromCol));
        san.append('x');
    }

    // ----------------------------------------------------
    // 4. Piece capture notation (Nxe5)
    // ----------------------------------------------------
    else if (captured != EMPTY || m.wasEnPassant) {
        san.append('x');  // piece letter already appended
    }

    // ----------------------------------------------------
    // 5. Target square
    // ----------------------------------------------------
    san.append(squareToString(m.to));

    // ----------------------------------------------------
    // 6. Promotion
    // ----------------------------------------------------
    if (m.wasPromotion) {
        san.append('=');
        QChar promoChar;

        switch (m.promotedTo) {
        case WQ: case BQ: promoChar = 'Q'; break;
        case WR: case BR: promoChar = 'R'; break;
        case WN: case BN: promoChar = 'N'; break;
        case WB: case BB: promoChar = 'B'; break;
        default: promoChar = 'Q'; break;
        }

        san.append(promoChar);
    }

    // ----------------------------------------------------
    // 7. Check / Checkmate
    // ----------------------------------------------------
    auto replies = mg.generateLegalMoves(after);

    if (replies.empty()) {
        int kingSq = mg.findKing(after, after.isWhiteTurn);
        bool inCheck = mg.isSquareAttacked(after, kingSq, !after.isWhiteTurn);

        if (inCheck)
            san.append('#');  // checkmate
        // else stalemate: no symbol needed
    }
    else {
        int kingSq = mg.findKing(after, after.isWhiteTurn);
        if (mg.isSquareAttacked(after, kingSq, !after.isWhiteTurn))
            san.append('+');
    }

    return san;
}

void MainWindow::addMoveToHistory(const Move& m, board& before, board& after)
{
    QString san = toSAN(m, before, after);

    bool whiteMove = before.isWhiteTurn;

    if (whiteMove) {
        // Create new row
        int moveNumber = (moveHistory.size() / 2) + 1;
        QString row = QString("%1. %2").arg(moveNumber).arg(san);

        moveHistoryList->addItem(row);
        moveHistoryList->setCurrentRow(moveHistoryList->count() - 1);
    }
    else {
        // Append black move to last row
        int rowIndex = moveHistoryList->count() - 1;
        if (rowIndex >= 0) {
            QString existing = moveHistoryList->item(rowIndex)->text();
            existing += " " + san;
            moveHistoryList->item(rowIndex)->setText(existing);
            moveHistoryList->setCurrentRow(rowIndex);
        }
    }
}

void MainWindow::truncateHistory() {
    // Keep positions up to currentMoveIndex, discard “future” branch
    if (currentMoveIndex + 1 < (int)positionHistory.size()) {
        positionHistory.resize(currentMoveIndex + 1);
        moveHistory.resize(currentMoveIndex);

        // Also remove rows from the list widget
        while (moveHistoryList->count() > (currentMoveIndex + 1) / 2) {
            delete moveHistoryList->takeItem(moveHistoryList->count() - 1);
        }
    }
}

void MainWindow::highlightLastMove()
{
    // Reset board first
    resetColors();

    if (lastFrom == -1 || lastTo == -1) return;

    auto highlightColor = [&](int r, int c) {
        bool isLight = ((r + c) % 2 == 0);
        // Official chess.com highlight colors
        return isLight ? "#f7f683" : "#baca44";
        };

    // FROM
    {
        int fr = lastFrom / 8;
        int fc = lastFrom % 8;
        QString color = highlightColor(fr, fc);
        boardButtons[fr][fc]->setStyleSheet("background-color: " + color + "; border: none;");
    }

    // TO
    {
        int tr = lastTo / 8;
        int tc = lastTo % 8;
        QString color = highlightColor(tr, tc);
        boardButtons[tr][tc]->setStyleSheet("background-color: " + color + "; border: none;");
    }

    /*uint64_t full = g_zobrist.computeHash(gameBoard);
    if (full != gameBoard.hashKey) {
		qDebug() << "Hash mismatch!";
    }*/
}


bool MainWindow::checkDrawByRepetitionOr50()
{
    // Current position hash
    uint64_t cur = g_zobrist.computeHash(gameBoard);

    int count = 0;
    for (const board& pos : positionHistory) {
        if (g_zobrist.computeHash(pos) == cur)
            ++count;
    }

    if (count >= 3) {
        turnLabel->setText("Draw by Threefold Repetition");
        return true;
    }

    if (gameBoard.halfmoveClock >= 100) {
        turnLabel->setText("Draw by 50-move Rule");
        return true;
    }

    return false;
}
