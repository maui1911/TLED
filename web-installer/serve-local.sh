#!/bin/bash
# Serve the web installer locally for testing
# Note: Web Serial requires HTTPS, so this won't work for actual flashing
# but it's useful for testing the UI

echo "Starting local server at http://localhost:8080"
echo "Note: Web Serial requires HTTPS, so flashing won't work locally"
echo "      This is just for testing the UI"
echo ""
echo "Press Ctrl+C to stop"

cd "$(dirname "$0")"
python3 -m http.server 8080
