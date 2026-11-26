#!/bin/sh
# Start the LoRa chat. Creates a local virtualenv with pyserial on first run.
set -e
cd "$(dirname "$0")"
if [ ! -x .venv/bin/python ]; then
  echo "Setting up .venv (first run)"
  python3 -m venv .venv
fi
if ! .venv/bin/python -c "import serial" 2>/dev/null; then
  .venv/bin/python -m pip install --quiet --disable-pip-version-check pyserial
fi
exec .venv/bin/python chat.py "$@"
