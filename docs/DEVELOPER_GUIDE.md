# 🛠 Инструкция разработчику: запуск Kolibri Cluster Node

## 1. Предварительные требования
- **ОС:** Linux или macOS (Unix-системы)
- **Компилятор:** GCC (или совместимый C11)
- **Библиотеки:** OpenSSL (`libssl-dev` для Linux, `brew install openssl` для macOS)
- **Инструменты:** GNU Make, Git, VS Code (рекомендуется)

---

## 2. Клонирование проекта
```sh
git clone <URL_репозитория> kolibri
cd kolibri
```

---

## 3. Создание ключа root.key
Файл `root.key` используется для аутентификации узлов.
```sh
openssl rand -hex 32 > root.key
```

---

## 4. Сборка проекта

**Через Make:**
```sh
make
```
👉 бинарник появится в `bin/kolibri_node_v1`

**Вручную:**
```sh
gcc -O2 -std=c11 src/kolibri_node_v1.c -o bin/kolibri_node_v1 -lcrypto -lpthread
```

---

## 5. Запуск узлов

**Один узел:**
```sh
./bin/kolibri_node_v1 --id nodeA --port 9000 --data chainA.db --root-key root.key
```

**Два узла (nodeA ↔ nodeB):**
```sh
./bin/kolibri_node_v1 --id nodeA --port 9000 --data chainA.db --root-key root.key
./bin/kolibri_node_v1 --id nodeB --port 9001 --data chainB.db --root-key root.key --peer 127.0.0.1:9000
```

**Кластер из 10 узлов:**
```sh
bash scripts/run_cluster.sh 10 9000
```

---

## 6. Запуск в VS Code
В `.vscode/tasks.json` уже настроены задачи:
- **Build Kolibri Node** → сборка (Ctrl+Shift+B)
- **Run Single Kolibri Node** → запуск одного узла
- **Run Two Kolibri Nodes** → запуск nodeA + nodeB
- **Run 10 Kolibri Nodes Cluster** → запуск 10 узлов
- **Run Custom Kolibri Cluster** → можно задать количество узлов и порт

---

## 7. Проверка обмена
- В логах должны появиться сообщения вида:
  ```
  Kolibri node started: id=nodeA port=9000 peers=0 data=chainA.db
  <- HELLO [HELLO:nodeB:...]
  ```
- Если видишь HELLO или ACK — узлы связались успешно.

---

## 8. Масштабирование
- Количество узлов можно увеличивать скриптом `scripts/run_cluster.sh`.
- Для распределённого запуска используем Docker или Kubernetes.

---

## 🔒 Важно
В MVP используется HMAC-SHA256 для проверки сообщений, но без TLS/SSL. В продакшене нужно будет добавить полноценное шифрование.

---

## Сборка и зависимости

### Для Ubuntu/Debian
```sh
sudo apt-get update
sudo apt-get install libssl-dev
```

### Для macOS (Homebrew)
```sh
brew install openssl
```

### Если компилятор не находит OpenSSL автоматически, добавьте пути вручную:
```sh
gcc -O2 -std=c11 src/kolibri_node_v1.c -o bin/kolibri_node_v1 -I/usr/local/opt/openssl/include -L/usr/local/opt/openssl/lib -lcrypto -lpthread
```
