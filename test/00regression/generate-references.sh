#!/usr/bin/env bash
# Generate (or regenerate) reference screenshots for the regression test suite.
# Usage: bash test/generate-references.sh [test_name...]

exec bash "$(dirname "$0")/regression.sh" --update "$@"
