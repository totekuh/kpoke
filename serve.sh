#!/bin/sh
cd dist && python3 -m http.server "${1:-8888}"
