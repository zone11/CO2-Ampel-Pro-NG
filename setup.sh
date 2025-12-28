#!/bin/bash
# CO2-Ampel Pro NG Setup Script
# This script installs PlatformIO and sets up the development environment

set -e

echo "========================================="
echo "CO2-Ampel Pro NG - Setup Script"
echo "========================================="
echo ""

# Check if Python is installed
if ! command -v python3 &> /dev/null; then
    echo "Error: Python 3 is not installed"
    echo "Please install Python 3 first:"
    echo "  brew install python3"
    exit 1
fi

echo "Python 3 found: $(python3 --version)"
echo ""

# Check if pip is installed
if ! command -v pip3 &> /dev/null; then
    echo "Error: pip3 is not installed"
    echo "Installing pip..."
    python3 -m ensurepip --upgrade
fi

echo "pip3 found: $(pip3 --version)"
echo ""

# Check if PlatformIO is already installed
if command -v pio &> /dev/null; then
    echo "PlatformIO is already installed: $(pio --version)"
    echo ""
else
    echo "Installing PlatformIO..."
    echo ""

    # Install PlatformIO via pip
    pip3 install --upgrade platformio

    # Add PlatformIO to PATH if needed
    if [[ ":$PATH:" != *":$HOME/.platformio/penv/bin:"* ]]; then
        echo ""
        echo "Adding PlatformIO to PATH..."
        echo 'export PATH="$HOME/.platformio/penv/bin:$PATH"' >> ~/.zshrc
        echo 'export PATH="$HOME/.platformio/penv/bin:$PATH"' >> ~/.bash_profile
        export PATH="$HOME/.platformio/penv/bin:$PATH"
    fi

    echo ""
    echo "PlatformIO installed successfully!"
    echo ""
fi

# Update PlatformIO platforms and packages
echo "Updating PlatformIO platforms..."
pio upgrade
pio platform update

echo ""
echo "Installing project dependencies..."
cd "$(dirname "$0")"
pio lib install

echo ""
echo "========================================="
echo "Setup Complete!"
echo "========================================="
echo ""
echo "You can now build the firmware with:"
echo "  pio run -e co2ampel_pro"
echo ""
echo "To upload to hardware:"
echo "  pio run -e co2ampel_pro -t upload"
echo ""
echo "To monitor serial output:"
echo "  pio device monitor -b 9600"
echo ""
echo "For more information, see README.md"
echo "========================================="
