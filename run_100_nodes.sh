#!/bin/bash
SEED_HOST=127.0.0.1
SEED_PORT=9000
SEED_DIGIT=1

# Создаем директорию для логов, если ее нет
mkdir -p testlogs

for i in $(seq 1 100); do
  ID="test$i"
  PORT=$((9000 + i))
  DATA="test_chain$i.db"

  # Если это первый узел, запускаем его с SEED_DIGIT и без соседей
  if [ $i -eq $SEED_DIGIT ]; then
    ./bin/kolibri_node_v1 --id $ID --port $PORT --data $DATA --root-key root.key --pilot_mode > testlogs/${ID}_port${PORT}.log 2>&1 &
  else
    # Для остальных узлов указываем первого узла как соседа с SEED_DIGIT
    ./bin/kolibri_node_v1 --id $ID --port $PORT --data $DATA --root-key root.key --pilot_mode --peer $SEED_HOST:$SEED_PORT > testlogs/${ID}_port${PORT}.log 2>&1 &
  fi

  # Небольшая задержка перед запуском следующего узла
  sleep 0.05

done

wait
