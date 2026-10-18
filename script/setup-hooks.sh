#!/bin/bash
set -e

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if git -C "$REPO_ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    HOOK_SOURCE="$REPO_ROOT/.githooks/pre-commit"

    if [ ! -f "$HOOK_SOURCE" ]; then
        echo "❌ [mylog] Hook source file not found: $HOOK_SOURCE" >&2
        exit 1
    fi

    chmod +x "$HOOK_SOURCE"

    git -C "$REPO_ROOT" config core.hooksPath .githooks

    echo "✅ [mylog] Git pre-commit hook installed successfully! Code formatting will be verified on commit."
else
    echo "⚠️  [mylog] Not a Git repository. Skipping hook installation."
fi
