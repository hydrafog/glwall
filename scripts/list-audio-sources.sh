#!/usr/bin/env bash
# Helper script to list PulseAudio sources for use with glwall --audio-device

echo "Available PulseAudio sources:"
echo "============================="
echo ""

# Check if we're in a nix shell with pw-cli
if command -v pw-cli &> /dev/null; then
    echo "Using pipewire-pulse (pw-cli):"
    pw-cli ls Node | grep -E "(node.name|node.description)" | sed 's/^/  /'
    echo ""
fi

# Try pactl if available
if command -v pactl &> /dev/null; then
    echo "Using PulseAudio (pactl):"
    pactl list sources short
    echo ""
    echo "Monitor sources (for capturing system audio):"
    pactl list sources short | grep monitor
    echo ""
    echo "Example usage:"
    echo "  glwall -s shader.frag --audio --audio-device <source-name>"
    echo ""
    echo "To capture system audio, use a .monitor source"
else
    echo "pactl not found. Install pulseaudio-utils or pipewire-pulse"
fi
