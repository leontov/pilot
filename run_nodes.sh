#!/bin/bash
# Скрипт для запуска множества узлов kolibri_node_v1 и связывания с веб-интерфейсом

MAX_NODES=300
BASE_PORT=9000

# Если передан аргумент --kill, убиваем все процессы kolibri_node_v1
if [[ "$1" == "--kill" ]]; then
    echo "🛑 Останавливаем все процессы kolibri_node_v1..."
    pkill -f kolibri_node_v1 && echo "Все процессы остановлены"
    exit 0
fi

echo "🛑 Останавливаем старые процессы kolibri_node_v1..."
pkill -f kolibri_node_v1 2>/dev/null
sleep 1

started=0

for ((digit=0; digit<MAX_NODES; digit++)); do
    port=$((BASE_PORT + digit))

    # Проверка: если порт занят — пропускаем
    if lsof -i :$port >/dev/null 2>&1; then
        echo "⚠️ Порт $port уже занят, узел $digit не будет запущен"
        continue
    fi

    echo "Запуск узла $digit на порту $port"
    # Узел запускается в фоне, можно передать параметр для веб-интерфейса
    ./bin/kolibri_node_v1 --id node$digit --port $port --digit $digit &
    sleep 0.1
    ((started++))
done

echo "✅ Запущено $started узлов из $MAX_NODES"

# Опционально: проверка веб-интерфейса
WEB_PORT=8888
if lsof -i :$WEB_PORT >/dev/null 2>&1; then
    echo "🌐 Веб-интерфейс доступен на порту $WEB_PORT"
else
    echo "⚠️ Веб-интерфейс не запущен на порту $WEB_PORT"
fi