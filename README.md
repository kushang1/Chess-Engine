# ChessGamev2

A native C++ chess project with a standalone UCI engine and a Qt desktop application. ChessGamev2 is built as a practical chess-engine playground: it includes a full move generator, alpha-beta search, evaluation, profiling tools, a graphical board UI, and support for modern engine features such as transposition tables, opening books, Syzygy tablebases, and runtime SMP search.

## Features

- C++17 chess engine library
- UCI-compatible command-line engine
- Qt Widgets desktop GUI
- Legal move generation
- FEN loading and export
- UCI and SAN move conversion
- Perft and divide support
- Iterative deepening alpha-beta search
- Principal variation search
- Aspiration windows
- Quiescence search
- Transposition table
- Killer move and history heuristics
- Null-move pruning
- Late move reductions and pruning
- Opening book support
- Syzygy tablebase support
- Runtime multi-threaded Lazy SMP search
- Engine profiling and benchmark commands

## Repository Layout

```text
ChessGamev2/
  Chess.sln
  Book/                    Optional Polyglot opening books
  syzygy/                  Optional Syzygy tablebases
  src/
    ChessEngine/           Core chess engine DLL
    ChessUci/              UCI console executable
    ChessQtApp/            Qt desktop application
  tests/                   Test and experiment area
```

## Requirements

- Windows
- Visual Studio 2022
- MSVC v143 toolset
- Windows 10 SDK
- Qt 6.x for the optional desktop GUI

The included Visual Studio project is configured for Qt installed at:

```text
C:\Qt\6.10.1\msvc2022_64
```

If your Qt installation uses a different path, update the Qt/MSBuild configuration in Visual Studio.

The desktop app is optional. If you only want to build the engine library and UCI executable, unload or remove the `ChessQtApp` project from the Visual Studio solution before building. This avoids the Qt dependency and still builds the core engine and UCI engine.

## Building

Open `Chess.sln` in Visual Studio and build `Release | x64`, or build from PowerShell:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe' Chess.sln /p:Configuration=Release /p:Platform=x64
```

Build outputs are written to:

```text
x64/Release/
```

Important binaries:

```text
ChessEngine.dll     Core engine library
ChessUci.exe        UCI engine executable
Chess.exe           Qt desktop application
```

If Qt is not installed, build only `ChessEngine` and `ChessUci`, or unload/remove `ChessQtApp` from the solution.

## Running the UCI Engine

```powershell
.\x64\Release\ChessUci.exe
```

Example UCI session:

```text
uci
isready
setoption name Hash value 256
setoption name Threads value 8
position startpos
go movetime 1000
```

Supported UCI options:

```text
Hash          Transposition table size in MB
Threads       Search worker count
Move Overhead Time-management safety margin in milliseconds
```

Useful UCI commands:

```text
go depth 8
go movetime 1000
go nodes 1000000
go perft 5
benchprofile
evalbench
profile reset
profile report
```

## Running the Desktop App

```powershell
.\x64\Release\Chess.exe
```

The desktop application uses the same engine library as the UCI executable and provides a graphical chess board, game controls, move interaction, and engine play through the Qt interface.

## Engine Overview

The engine uses a conventional alpha-beta search architecture with iterative deepening. The main search combines principal variation search, aspiration windows, transposition-table cutoffs, null-move pruning, late move reductions, move ordering, quiescence search, and tactical pruning.

Runtime SMP is implemented with a Lazy SMP architecture. Each worker owns its own board and search state, while workers share stop/time control and the transposition table. This keeps the implementation stable and scalable without introducing split-point search complexity.

The transposition table is designed for concurrent search. Entries are packed into atomic payloads and validated with a key-derived verification word so concurrent replacement is safe and does not require a global TT mutex.

## Opening Books

Opening books can be placed in the `Book/` directory. The engine searches common book filenames such as:

```text
Human.bin
Perfect2023.bin
komodo.bin
book.bin
```

You can also point the engine to a specific book using environment variables:

```text
CHESS_BOOK_FILE
CHESS_BOOK
```

## Syzygy Tablebases

Syzygy tablebases can be placed in the `syzygy/` directory. You can also configure a custom path with:

```text
CHESS_SYZYGY_PATH
```

Tablebases are optional. The engine runs normally without them.

## Testing

Useful validation commands:

```text
go perft 5
benchprofile
evalbench
```

Recommended engine validation before releases:

- Perft regression suite
- Tactical position benchmark
- Fixed-depth search benchmark
- Fixed-node search benchmark
- Self-play matches across multiple time controls
- Thread-scaling tests for `Threads=1`, `2`, `4`, `8`, and higher
- Hash-size stress tests

## Development Notes

This repository is useful for experimenting with chess-engine ideas. When making engine changes, prefer small measurable patches and test them with fixed seeds/openings whenever possible. Strength changes should be validated through self-play or SPRT-style testing rather than only NPS.

Areas worth improving:

- Automated perft test suite
- CI build for the engine and UCI target
- Reproducible self-play harness
- Fixed-node benchmark command
- More detailed UCI search statistics
- Evaluation tuning
- Time-management tuning
- Release packaging for the GUI and UCI engine

## License

This project is released under the MIT License. See [LICENSE](LICENSE).

Third-party libraries, tablebase code, Qt, opening books, and external assets may have their own licenses. Those components remain under their respective license terms.
