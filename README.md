# routerAI

Console-first AI account router written in C++20.

The first milestone focuses on a small, testable core before adding OAuth, quota tracking, routing, and an OpenAI-compatible proxy.

## Current scope

- C++20 + CMake
- CLI commands
  - `router status`
  - `router account list`
  - `router account add codex`
- SQLite account persistence
- Provider abstraction
- Codex provider stub (OAuth is the next milestone)

## Planned milestones

1. Console skeleton + SQLite persistence
2. Codex OAuth login and credential storage
3. Account health and quota tracking
4. Single-account request path
5. Multi-account routing, cooldown, and failover
6. OpenAI-compatible local API
7. Additional providers
8. Web/desktop management UI

## Build with vcpkg

Requirements:

- CMake 3.24+
- C++20 compiler
- vcpkg

```bash
git clone https://github.com/Vo-Xuan-Duong/routerAI.git
cd routerAI

cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake

cmake --build build --config Release
```

On Windows/Visual Studio, the executable is typically under `build/Release/router.exe`.

## Usage

```bash
router status
router account list
router account add codex
```

`account add codex` currently creates a local placeholder account. OAuth authentication will replace this stub in the next milestone.

## Data

The router stores local state in `router.db` by default. Runtime database files are ignored by Git.
