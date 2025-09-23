#!/bin/bash

# Остановка существующих процессов
pkill -f "kolibri_node"

# Создание build директории
mkdir -p build
cd build

# Конфигурация и сборка
cmake ..
make -j4

if [ $? -ne 0 ]; then
    echo "Build failed"
    exit 1
fi

# Запуск тестового узла
./kolibri_node 9002 memory
