#!/bin/bash

# Build script for Modular Pipeline System
# This script compiles the main application and all plugins

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Print functions
print_status() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

# Create output directory
print_status "Creating output directory..."
mkdir -p output

# Compile main application
print_status "Building main application..."
gcc -Wall -Wextra -std=c99 -o output/analyzer \
    main.c \
    -ldl -lpthread \
    || {
        print_error "Failed to build main application"
        exit 1
    }

# List of plugins to build
plugins=("logger" "uppercaser" "rotator" "flipper" "expander" "typewriter")

# Build each plugin
for plugin_name in "${plugins[@]}"; do
    print_status "Building plugin: $plugin_name"
    gcc -fPIC -shared -Wall -Wextra -std=c99 -o output/${plugin_name}.so \
        plugins/${plugin_name}.c \
        plugins/plugin_common.c \
        plugins/sync/monitor.c \
        plugins/sync/consumer_producer.c \
        -ldl -lpthread \
        || {
            print_error "Failed to build $plugin_name"
            exit 1
        }
done

print_status "Build completed successfully!"
print_status "Main application: output/analyzer"
print_status "Plugins: output/*.so"

# List built files
print_status "Built files:"
ls -la output/
