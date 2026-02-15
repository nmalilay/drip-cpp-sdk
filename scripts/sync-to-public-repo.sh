#!/bin/bash
# =============================================================================
# Sync Drip C++ SDK to Public GitHub Repository
# =============================================================================
#
# This script syncs the C++ SDK from the monorepo to a standalone public repo.
# Mirrors the pattern used by packages/sdk/scripts/sync-to-public-repo.sh
#
# Prerequisites:
#   - GitHub CLI (gh) installed and authenticated
#   - Git installed
#   - Write access to the public repo
#
# Usage:
#   ./scripts/sync-to-public-repo.sh                         # Dry run
#   ./scripts/sync-to-public-repo.sh --push                  # Sync and push
#   ./scripts/sync-to-public-repo.sh --create-release 0.1.0  # Sync, push, release
#
# =============================================================================

set -e

# Configuration - update this to your public repo
PUBLIC_REPO="MichaelLevin5908/drip-sdk-cpp"
PUBLIC_REPO_URL="https://github.com/$PUBLIC_REPO.git"
BRANCH="main"

# Directories to sync
SYNC_DIRS=(
    "include"
    "src"
    "examples"
    ".github"
)

# Files to sync
SYNC_FILES=(
    "CMakeLists.txt"
    "Makefile"
    "LICENSE"
    "README.md"
    "CONTRIBUTING.md"
)

# Parse arguments
PUSH=false
CREATE_RELEASE=""

while [[ $# -gt 0 ]]; do
    case $1 in
        --push)
            PUSH=true
            shift
            ;;
        --create-release)
            CREATE_RELEASE="$2"
            PUSH=true
            shift 2
            ;;
        -h|--help)
            echo "Usage: $0 [--push] [--create-release VERSION]"
            echo ""
            echo "Options:"
            echo "  --push              Push changes to public repo"
            echo "  --create-release    Create a GitHub release (implies --push)"
            echo "  -h, --help          Show this help message"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Get the directory of this script
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK_DIR="$(dirname "$SCRIPT_DIR")"
TEMP_DIR=$(mktemp -d)

echo "========================================"
echo "Drip C++ SDK - Sync to Public Repo"
echo "========================================"
echo ""
echo "Source:      $SDK_DIR"
echo "Target repo: $PUBLIC_REPO"
echo "Push:        $PUSH"
if [[ -n "$CREATE_RELEASE" ]]; then
    echo "Release:     v$CREATE_RELEASE"
fi
echo ""

# Check prerequisites
if ! command -v gh &> /dev/null; then
    echo "Error: GitHub CLI (gh) is not installed."
    echo "Install from: https://cli.github.com/"
    exit 1
fi

if ! gh auth status &> /dev/null; then
    echo "Error: GitHub CLI is not authenticated."
    echo "Run: gh auth login"
    exit 1
fi

# Clone the public repo
echo "Step 1: Cloning public repo..."
git clone "$PUBLIC_REPO_URL" "$TEMP_DIR" --depth 1
cd "$TEMP_DIR"

# Remove old files (except .git)
echo ""
echo "Step 2: Removing old files..."
find . -maxdepth 1 ! -name '.git' ! -name '.' -exec rm -rf {} +

# Copy SDK files
echo ""
echo "Step 3: Copying SDK files..."

# Copy directories
for dir in "${SYNC_DIRS[@]}"; do
    if [[ -d "$SDK_DIR/$dir" ]]; then
        echo "  Copying directory: $dir/"
        if command -v rsync &> /dev/null; then
            rsync -a "$SDK_DIR/$dir/" "$TEMP_DIR/$dir/"
        else
            cp -r "$SDK_DIR/$dir" "$TEMP_DIR/"
        fi
    else
        echo "  Skipping missing directory: $dir/"
    fi
done

# Copy files
for file in "${SYNC_FILES[@]}"; do
    if [[ -f "$SDK_DIR/$file" ]]; then
        echo "  Copying file: $file"
        cp "$SDK_DIR/$file" "$TEMP_DIR/"
    else
        echo "  Skipping missing file: $file"
    fi
done

# Create .gitignore
cat > "$TEMP_DIR/.gitignore" << 'EOF'
build/
third_party/
*.o
*.a
*.so
*.dylib
.DS_Store
CMakeCache.txt
CMakeFiles/
cmake_install.cmake
Makefile.cmake
EOF

# Show what changed
echo ""
echo "Step 4: Changes to be synced..."
git add -A
git status

if [[ "$PUSH" == "false" ]]; then
    echo ""
    echo "========================================"
    echo "DRY RUN COMPLETE"
    echo "========================================"
    echo ""
    echo "To push changes, run:"
    echo "  $0 --push"
    echo ""
    echo "To create a release, run:"
    echo "  $0 --create-release 0.1.0"
    echo ""
    echo "Temp directory: $TEMP_DIR"
    exit 0
fi

# Check if there are changes
if git diff --cached --quiet; then
    echo ""
    echo "No changes to sync."
    rm -rf "$TEMP_DIR"
    exit 0
fi

# Commit and push
echo ""
echo "Step 5: Committing changes..."
VERSION="${CREATE_RELEASE:-0.1.0}"
git commit -m "chore: sync from monorepo (v$VERSION)

Synced from drip monorepo sdk-cpp"

echo ""
echo "Step 6: Pushing to $PUBLIC_REPO..."
git push origin "$BRANCH"

echo ""
echo "========================================"
echo "SYNC COMPLETE"
echo "========================================"
echo ""
echo "Changes pushed to: https://github.com/$PUBLIC_REPO"

# Create release if requested
if [[ -n "$CREATE_RELEASE" ]]; then
    echo ""
    echo "Step 7: Creating GitHub release v$CREATE_RELEASE..."
    gh release create "v$CREATE_RELEASE" \
        --repo "$PUBLIC_REPO" \
        --title "v$CREATE_RELEASE" \
        --notes "Release v$CREATE_RELEASE

## Installation

### CMake (FetchContent)
\`\`\`cmake
include(FetchContent)
FetchContent_Declare(
    drip_sdk
    GIT_REPOSITORY https://github.com/$PUBLIC_REPO.git
    GIT_TAG        v$CREATE_RELEASE
)
FetchContent_MakeAvailable(drip_sdk)
target_link_libraries(your_target PRIVATE drip::sdk)
\`\`\`

### Manual
\`\`\`bash
git clone https://github.com/$PUBLIC_REPO.git
cd drip-sdk-cpp
make
\`\`\`"

    echo ""
    echo "Release created: https://github.com/$PUBLIC_REPO/releases/tag/v$CREATE_RELEASE"
fi

# Cleanup
rm -rf "$TEMP_DIR"
