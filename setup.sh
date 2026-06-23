#!/bin/bash
# Field Radar - macOS Development Environment Setup
# Run once after cloning: source setup.sh

ZEPHYR_TOOLS="$HOME/.zephyrtools"
ZEPHYR_BASE_PATH="$HOME/Documents/Zephyr_Radar/zephyr"
SHELL_PROFILE="$HOME/.zshrc"

# Detect shell
if [ -n "$BASH_VERSION" ]; then
    SHELL_PROFILE="$HOME/.bashrc"
fi

# Add to shell profile
cat >> "$SHELL_PROFILE" << EOF
# Field Radar - Zephyr Tools
source "\$HOME/.zephyrtools/env/bin/activate"
export ZEPHYR_BASE="$ZEPHYR_BASE_PATH"
EOF

echo "Profile updated at $SHELL_PROFILE"
echo "Run 'source $SHELL_PROFILE' or restart terminal to apply."