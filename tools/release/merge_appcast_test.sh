#!/bin/bash
set -euo pipefail
exec python3 -B tools/release/merge_appcast_test.py
