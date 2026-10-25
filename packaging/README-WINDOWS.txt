POLYOMINO SEARCH - WINDOWS x64

No compiler or installation is needed. Extract the entire ZIP first.

1. Open the extracted polyomino-search folder in File Explorer.
2. Click File Explorer's address bar (the folder path at the top).
3. Type cmd and press Enter.
4. Type a command below and press Enter.

Alternatively, double-click "Open Command Prompt.cmd" in this folder.
The .exe is a command-line program; run it from that command prompt.

SEARCH ALL 79 REQUESTED CLASSES FOR FIVE MINUTES:
polyomino-search.exe --threads 4 --seconds 300 --output results.jsonl

SEARCH ONE CLASS:
polyomino-search.exe --target 1/2/4 --max-each 20 --threads 4 --seconds 300

FIND AS MANY DIFFERENT SHAPES AS POSSIBLE:
polyomino-search.exe --many --threads 4 --seconds 300

STOP EACH WORKER AFTER THREE NEW SHAPES, OR AFTER FIVE MINUTES:
polyomino-search.exe --many --per-thread 3 --threads 4 --seconds 300

EXACT CHECK: FOUR OF EACH LABEL, AT MOST 16 CELLS:
polyomino-search.exe --mode exact --target 1/2/4/6 --min-each 4 --max-each 4 --max-cells 16 --threads 4 --seconds 0

SHOW ALL OPTIONS:
polyomino-search.exe --help

FOUND means a validated shape exists. Its cells are output as coordinates.
UNKNOWN means the search has not proved existence or impossibility.
IMPOSSIBLE results apply ONLY to the reported multiplicity/cell bounds.
--seconds 0 removes the time limit; exact searches can grow very expensive.
Press Ctrl+C to stop. Output files are replaced if you reuse their names.

Source and full instructions:
https://github.com/JagoZinga/polyomino-search
