# KolibriAI CMake configuration
cmake_minimum_required(VERSION 3.10)
project(KolibriAI C)

# Установка стандарта C и флагов компиляции
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -O3 -march=native -flto")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -flto")

# Поиск необходимых библиотек
find_package(CURL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONC REQUIRED json-c)
find_package(OpenSSL REQUIRED)

# Настройка путей для библиотек
include_directories(
    /opt/homebrew/opt/openssl@3/include
    ${JSONC_INCLUDE_DIRS}
)
link_directories(
    /opt/homebrew/opt/openssl@3/lib
    ${JSONC_LIBRARY_DIRS}
)

# Проверяем наличие json-c
if(NOT JSONC_FOUND)
    message(FATAL_ERROR "json-c library not found!")
endif()
include_directories(${JSONC_INCLUDE_DIRS})
link_directories(${JSONC_LIBRARY_DIRS})

# Указываем путь к заголовочным файлам libmicrohttpd
include_directories(/opt/homebrew/opt/libmicrohttpd/include)
link_directories(/opt/homebrew/opt/libmicrohttpd/lib)

# Добавление исходных файлов
set(SOURCES
	src/main.c
	src/decimal_cell.c
	src/rules_engine.c
	src/formula_advanced.c
	src/learning.c
	src/network.c
	src/blockchain.c
	src/formula.c
)

# Создание исполняемого файла
add_executable(kolibri_node ${SOURCES})

# Подключение заголовочных файлов
target_include_directories(kolibri_node PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/include
        ${CURL_INCLUDE_DIRS}
        ${JSONC_INCLUDE_DIRS}
)

# Подключение библиотек
target_link_libraries(kolibri_node
	${CURL_LIBRARIES}
	${JSONC_LIBRARIES}
	OpenSSL::SSL
	OpenSSL::Crypto
	m
	pthread
)

# Добавляем веб-интерфейс
add_executable(web_interface src/web_interface.c)
target_link_libraries(web_interface PRIVATE
	microhttpd
	${JSONC_LIBRARIES}
)

# Связываем abstract_mind.c с веб-интерфейсом
target_sources(web_interface PRIVATE src/abstract_mind.c src/brain.c)
target_sources(web_interface PRIVATE src/tfidf.c)
target_sources(web_interface PRIVATE src/embed.c src/mlp.c)
target_sources(web_interface PRIVATE src/node_brain.c src/rule_engine.c)
target_sources(web_interface PRIVATE src/arithmetic.c)
# Add the embedding cache module for faster lookups
target_sources(web_interface PRIVATE src/embed_cache.c)

# Link standard libs used by brain/web
target_link_libraries(web_interface PRIVATE m pthread)

# Simple tests
add_executable(test_arithmetic tests/test_arithmetic.c src/arithmetic.c)
target_link_libraries(test_arithmetic PRIVATE m)
add_executable(test_rule_engine tests/test_rule_engine.c src/rule_engine.c src/embed.c src/formula.c)
target_link_libraries(test_rule_engine PRIVATE ${JSONC_LIBRARIES} m)
add_executable(test_formula tests/test_formula.c src/formula.c src/formula_advanced.c src/decimal_cell.c)
target_link_libraries(test_formula PRIVATE ${JSONC_LIBRARIES} uuid m)
add_executable(test_decimal_cell tests/test_decimal_cell.c src/kolibri_decimal_cell.c)
target_link_libraries(test_decimal_cell PRIVATE m)
target_include_directories(test_formula PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/include
        ${JSONC_INCLUDE_DIRS})
add_executable(test_network tests/test_network.c src/network.c)
target_link_libraries(test_network PRIVATE pthread)
target_include_directories(test_network PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/include)

add_executable(test_blockchain tests/test_blockchain.c src/kovian_blockchain.c src/formula.c)
target_link_libraries(test_blockchain PRIVATE ${JSONC_LIBRARIES} OpenSSL::Crypto m)
target_include_directories(test_blockchain PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/include
        ${JSONC_INCLUDE_DIRS})


# Добавляем пути к заголовочным файлам для веб-интерфейса
target_include_directories(web_interface PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${JSONC_INCLUDE_DIRS}
)

# Install helper run script
install(FILES scripts/run_server.sh DESTINATION bin PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_READ WORLD_READ)
