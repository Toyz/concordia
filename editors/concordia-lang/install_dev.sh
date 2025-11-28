#!/bin/bash
set -e

# Configuration
EXTENSION_NAME="concordia-lang"
PUBLISHER="Helba"
SOURCE_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
VSCODE_EXTENSIONS_DIR="$HOME/.vscode/extensions"

# Check for npm
if ! command -v npm &> /dev/null; then
    echo "npm could not be found"
    exit 1
fi

# Read version from package.json
VERSION=$(node -p "require('$SOURCE_DIR/package.json').version")
TARGET_DIR_NAME="$PUBLISHER.$EXTENSION_NAME-$VERSION"
TARGET_DIR_PATH="$VSCODE_EXTENSIONS_DIR/$TARGET_DIR_NAME"

echo "Installing $TARGET_DIR_NAME..."

# Compile
echo "Compiling..."
cd "$SOURCE_DIR"
npm install
npm run compile

# Remove old versions
echo "Removing old versions..."
find "$VSCODE_EXTENSIONS_DIR" -maxdepth 1 -name "$PUBLISHER.$EXTENSION_NAME*" -exec rm -rf {} +

# Create Symlink
echo "Creating symlink at $TARGET_DIR_PATH..."
ln -s "$SOURCE_DIR" "$TARGET_DIR_PATH"

echo "Done! Please reload VS Code (Cmd+R or Developer: Reload Window)."
