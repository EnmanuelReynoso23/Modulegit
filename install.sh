#!/bin/bash
# install.sh — Install ModuleGit shell wrapper so 'git modgit' works
#
# Usage: bash install.sh
#   or:  bash install.sh --uninstall
#
# On Windows, may need to run from an elevated (admin) terminal
# if Git is installed in Program Files.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$SCRIPT_DIR/bin/git-modgit"

# Determine Git's exec-path (where git looks for subcommands)
GIT_EXEC_PATH="$(git --exec-path)"
DEST="$GIT_EXEC_PATH/git-modgit"

# Fallback: user-local bin directory (doesn't need admin)
USER_BIN="$HOME/bin"
USER_DEST="$USER_BIN/git-modgit"

if [ "$1" = "--uninstall" ]; then
    removed=0
    if [ -f "$DEST" ]; then
        rm -f "$DEST" 2>/dev/null && { echo "Removed $DEST"; removed=1; } || echo "Need admin to remove $DEST"
    fi
    if [ -f "$USER_DEST" ]; then
        rm -f "$USER_DEST" && { echo "Removed $USER_DEST"; removed=1; }
    fi
    [ $removed -eq 0 ] && echo "ModuleGit not found — nothing to uninstall."
    exit 0
fi

if [ ! -f "$SRC" ]; then
    echo "error: bin/git-modgit not found. Run this from the ModuleGit repo root." >&2
    exit 1
fi

echo "Installing ModuleGit..."

# Try git exec-path first (needs admin on Windows)
if cp "$SRC" "$DEST" 2>/dev/null; then
    chmod +x "$DEST"
    echo "  Installed to: $DEST"
    echo ""
    echo "Done! Test it: git modgit help"
else
    echo "  Cannot write to $GIT_EXEC_PATH (need admin)."
    echo "  Installing to user directory instead..."
    mkdir -p "$USER_BIN"
    cp "$SRC" "$USER_DEST"
    chmod +x "$USER_DEST"
    echo "  Installed to: $USER_DEST"

    # Check if ~/bin is in PATH
    if ! echo "$PATH" | grep -q "$USER_BIN"; then
        echo ""
        echo "  NOTE: Add ~/bin to your PATH. Add this to your ~/.bashrc:"
        echo "    export PATH=\"\$HOME/bin:\$PATH\""
    fi
    echo ""
    echo "Done! Test it: git modgit help"
fi

echo ""
echo "To uninstall: bash install.sh --uninstall"
