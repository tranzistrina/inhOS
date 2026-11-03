#!/bin/bash
set -e
cd "$(dirname "$0")"
trap 'echo "Ошибка запуска. Нужен Python 3.10+ с Tcl/Tk (python.org) и, при необходимости, драйвер CP210x."; read -r -p "Нажмите Enter…"' ERR
if [ ! -x .venv/bin/python ]; then
  python3 -m venv .venv
fi
if ! .venv/bin/python -c 'import serial, PIL' >/dev/null 2>&1; then
  .venv/bin/python -m pip install -r requirements.txt
fi
.venv/bin/python macos.py
