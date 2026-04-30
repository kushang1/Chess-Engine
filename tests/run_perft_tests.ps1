param(
    [string]$ExePath = ".\x64\Release\Chess.exe",
    [string]$WorkingDirectory = "."
)

$ErrorActionPreference = "Stop"

$resolvedWorkingDirectory = (Resolve-Path -LiteralPath $WorkingDirectory).Path
$resolvedExePath = if ([System.IO.Path]::IsPathRooted($ExePath)) {
    $ExePath
}
else {
    Join-Path $resolvedWorkingDirectory $ExePath
}

if (-not (Test-Path -LiteralPath $resolvedExePath)) {
    throw "Chess executable not found: $resolvedExePath. Build Release first."
}

$env:Path = "D:\Qt\6.11.0\msvc2022_64\bin;D:\vcpkg\installed\x64-windows\bin;$env:Path"

$positions = @(
    @{
        Name = "Start Position"
        Fen = $null
        Expected = @(20, 400, 8902, 197281, 4865609)
    },
    @{
        Name = "Kiwipete"
        Fen = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"
        Expected = @(48, 2039, 97862, 4085603)
    },
    @{
        Name = "En Passant Pins"
        Fen = "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1"
        Expected = @(14, 191, 2812)
    },
    @{
        Name = "Promotion And Castling"
        Fen = "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1"
        Expected = @(6, 264, 9467)
    },
    @{
        Name = "Promotion Checks"
        Fen = "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8"
        Expected = @(44, 1486, 62379)
    },
    @{
        Name = "Middle Game"
        Fen = "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10"
        Expected = @(46, 2079, 89890)
    }
)

$failures = @()

foreach ($position in $positions) {
    for ($i = 0; $i -lt $position.Expected.Count; ++$i) {
        $depth = $i + 1
        $expected = [int64]$position.Expected[$i]
        $resultPath = Join-Path $resolvedWorkingDirectory "perft_result.txt"
        Remove-Item -LiteralPath $resultPath -ErrorAction SilentlyContinue

        $arguments = if ($null -eq $position.Fen) {
            @("--perft", [string]$depth)
        }
        else {
            @("--perft", [string]$depth, "--fen", $position.Fen)
        }

        $process = Start-Process `
            -FilePath $resolvedExePath `
            -WorkingDirectory $resolvedWorkingDirectory `
            -ArgumentList $arguments `
            -Wait `
            -PassThru

        if ($process.ExitCode -ne 0) {
            $failures += "$($position.Name) depth $depth exited with code $($process.ExitCode)."
            continue
        }

        if (-not (Test-Path -LiteralPath $resultPath)) {
            $failures += "$($position.Name) depth $depth did not produce perft_result.txt."
            continue
        }

        $text = Get-Content -LiteralPath $resultPath -Raw
        if ($text -notmatch "nodes:\s+(\d+)") {
            $failures += "$($position.Name) depth $depth produced unparsable output: $text"
            continue
        }

        $actual = [int64]$Matches[1]
        if ($actual -eq $expected) {
            Write-Host ("PASS {0,-22} depth {1}: {2}" -f $position.Name, $depth, $actual)
        }
        else {
            $failures += "$($position.Name) depth $depth expected $expected but got $actual."
            Write-Host ("FAIL {0,-22} depth {1}: expected {2}, got {3}" -f $position.Name, $depth, $expected, $actual)
        }
    }
}

if ($failures.Count -gt 0) {
    Write-Host ""
    Write-Host "Perft failures:"
    foreach ($failure in $failures) {
        Write-Host "  $failure"
    }
    exit 1
}

Write-Host ""
Write-Host "All perft tests passed."
