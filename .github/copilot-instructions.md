# Project Guidelines

## Code Style
- Primary languages are C and C++ for Linux system programming exercises.
- Keep existing naming and comment style in each module (many comments are in Chinese).
- Prefer minimal edits and preserve local file style instead of broad refactors.

## Architecture
- This workspace is a collection of independent mini-projects, not a single monolith.
- Typical layout in many modules: `inc/` for headers, `src/` for implementation, `test/` for entry program.
- Key learning modules:
  - `Process_Poll/`: process pool + pipe-based task dispatch.
  - `Shell/`: mini shell command execution and built-in behavior.
  - `Exec/`: process replacement (`exec*`) experiments.
  - `MuchFile/`: large multi-file build stress example.

## Build and Test
- Do not assume a root-level build; build in the target subdirectory.
- Verified commands:
  - `cd Process_Poll && make release|debug|run|clean`
  - `cd Shell && make release|debug|run|clean`
  - `cd Exec && make run|clean`
  - `cd MuchFile && make|clean`
- VS Code tasks are configured for some modules (for example `Shell: make debug`).

## Conventions
- Many Makefiles inject system headers through compiler flags (for example `-include stdio.h -include unistd.h -include stdlib.h`), so check Makefile before changing includes.
- For process-related code, preserve parent/child fd lifecycle order (`pipe` -> `fork` -> close unused ends -> dispatch -> `waitpid`).
- When searching or large refactors are needed, avoid scanning `MuchFile/` unless that directory is the target.
