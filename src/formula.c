#include "formula.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <uuid/uuid.h>
#include <json-c/json.h>

// Определения типов формул
const int FORMULA_TYPE_SIMPLE = 0;
const int FORMULA_TYPE_POLYNOMIAL = 1;
const int FORMULA_TYPE_COMPOSITE = 2;
const int FORMULA_TYPE_PERIODIC = 3;

// Тестовые точки для оценки формулы
static const double test_points[] = {-10.0, -5.0, -2.0, -1.0, -0.5, 0.0, 0.5, 1.0, 2.0, 5.0, 10.0};
static const int num_test_points = sizeof(test_points) / sizeof(test_points[0]);

// Создание коллекции формул
FormulaCollection* formula_collection_create(size_t initial_capacity) {
    FormulaCollection* collection = malloc(sizeof(FormulaCollection));
    if (!collection) return NULL;
    
    collection->formulas = malloc(sizeof(Formula) * initial_capacity);
    if (!collection->formulas) {
        free(collection);
        return NULL;
    }
    
    collection->count = 0;
    collection->capacity = initial_capacity;
    return collection;
}

// Уничтожение коллекции
void formula_collection_destroy(FormulaCollection* collection) {
    if (collection) {
        free(collection->formulas);
        free(collection);
    }
}

// Добавление формулы в коллекцию
int formula_collection_add(FormulaCollection* collection, const Formula* formula) {
    if (!collection || !formula) return -1;
    
    if (collection->count >= collection->capacity) {
        size_t new_capacity = collection->capacity * 2;
        Formula* new_formulas = realloc(collection->formulas, sizeof(Formula) * new_capacity);
        if (!new_formulas) return -1;
        
        collection->formulas = new_formulas;
        collection->capacity = new_capacity;
    }
    
    memcpy(&collection->formulas[collection->count], formula, sizeof(Formula));
    collection->count++;
    return 0;
}

// Поиск формулы по ID
Formula* formula_collection_find(FormulaCollection* collection, const char* id) {
    if (!collection || !id) return NULL;
    
    for (size_t i = 0; i < collection->count; i++) {
        if (strcmp(collection->formulas[i].id, id) == 0) {
            return &collection->formulas[i];
        }
    }
    return NULL;
}

// Распознавание типа формулы
int get_formula_type(const char* content) {
    if (!content) return FORMULA_TYPE_SIMPLE;
    
    if (strstr(content, "sin") || strstr(content, "cos"))
        return FORMULA_TYPE_PERIODIC;
    else if (strstr(content, "^"))
        return FORMULA_TYPE_POLYNOMIAL;
    else if (strstr(content, "+") || strstr(content, "*"))
        return FORMULA_TYPE_COMPOSITE;
        
    return FORMULA_TYPE_SIMPLE;
}

// Вычисляет значение формулы для заданного x
static double evaluate_formula(const char* content, double x) {
    int type = get_formula_type(content);
    double result = 0.0;
    
    switch(type) {
        case FORMULA_TYPE_POLYNOMIAL: {
            int coef, power, offset;
            if (sscanf(content, "f(x) = %d * x^%d + %d", &coef, &power, &offset) == 3) {
                result = coef * pow(x, power) + offset;
            }
            break;
        }
        case FORMULA_TYPE_COMPOSITE: {
            int coef1, coef2;
            if (sscanf(content, "f(x) = %d * x^2 + %d * x", &coef1, &coef2) == 2) {
                result = coef1 * pow(x, 2) + coef2 * x;
            }
            break;
        }
        case FORMULA_TYPE_PERIODIC: {
            int amp, freq;
            if (sscanf(content, "f(x) = %d * sin(%d * x)", &amp, &freq) == 2) {
                result = amp * sin(freq * x);
            }
            break;
        }
        default: { // FORMULA_TYPE_SIMPLE или неизвестный тип
            int base, coef;
            if (sscanf(content, "f(x) = %d * %d^x", &coef, &base) == 2) {
                result = coef * pow(base, x);
            }
            break;
        }
    }
    
    return result;
}

// Валидация формулы
int validate_formula(const Formula* formula) {
    if (!formula) return 0;
    if (strlen(formula->content) == 0) return 0;
    if (formula->effectiveness < 0.0 || formula->effectiveness > 1.0) return 0;
    return 1;
}

// Генерация случайной формулы
Formula* generate_random_formula(int complexity_level) {
    Formula* formula = malloc(sizeof(Formula));
    if (!formula) return NULL;
    
    // Генерация уникального ID
    uuid_t uuid;
    uuid_generate(uuid);
    uuid_unparse(uuid, formula->id);
    
    // Более разнообразная генерация формул
    int type = rand() % 4;  // Тип формулы
    char formula_str[sizeof(formula->content)];
    
    switch(type) {
        case 0: {  // Полиномиальная
            int coef = (rand() % 19) - 9;
            int power = (rand() % complexity_level) + 1;
            int offset = (rand() % 21) - 10;
            if (coef == 0) coef = 1;
            snprintf(formula_str, sizeof(formula_str),
                    "f(x) = %d * x^%d + %d", coef, power, offset);
            break;
        }
        case 1: {  // Композитная
            int coef1 = (rand() % 9) + 1;
            int coef2 = (rand() % 9) + 1;
            snprintf(formula_str, sizeof(formula_str),
                    "f(x) = %d * x^2 + %d * x", coef1, coef2);
            break;
        }
        case 2: {  // Периодическая
            int amp = (rand() % 5) + 1;
            int freq = (rand() % 3) + 1;
            snprintf(formula_str, sizeof(formula_str),
                    "f(x) = %d * sin(%d * x)", amp, freq);
            break;
        }
        default: {  // Экспоненциальная
            int base = (rand() % 3) + 2;
            int coef = (rand() % 5) + 1;
            snprintf(formula_str, sizeof(formula_str),
                    "f(x) = %d * %d^x", coef, base);
            break;
        }
    }
    
    strncpy(formula->content, formula_str, sizeof(formula->content) - 1);
    formula->content[sizeof(formula->content) - 1] = '\0';
    
    formula->effectiveness = 0.0;
    formula->created_at = time(NULL);
    formula->tests_passed = 0;
    formula->confirmations = 0;
    
    return formula;
}

// Оценка эффективности формулы
double evaluate_effectiveness(const Formula* formula) {
    if (!formula) return 0.0;
    
    // Веса для различных критериев
    const double complexity_weight = 0.2;
    const double novelty_weight = 0.2;
    const double stability_weight = 0.2;
    const double pattern_weight = 0.2;
    const double efficiency_weight = 0.2;
    
    // 1. Вычисляем результаты для всех тестовых точек
    double results[num_test_points];
    clock_t eval_start = clock();
    
    for(int i = 0; i < num_test_points; i++) {
        results[i] = evaluate_formula(formula->content, test_points[i]);
        // Проверка на корректность результатов
        if (isinf(results[i]) || isnan(results[i])) {
            return 0.0;
        }
    }
    
    double eval_time = ((double)(clock() - eval_start)) / CLOCKS_PER_SEC;
    
    // 2. Оценка сложности
    int num_operators = 0;
    for(const char* c = formula->content; *c; c++) {
        if(*c == '+' || *c == '-' || *c == '*' || *c == '/' || *c == '^')
            num_operators++;
    }
    double complexity_score = (num_operators > 0) ? fmin(num_operators / 5.0, 1.0) : 0.0;
    
    // 3. Анализ результатов
    double min_val = results[0], max_val = results[0];
    double prev_diff = results[1] - results[0];
    int is_monotonic = 1;
    int inflection_points = 0;
    
    for(int i = 1; i < num_test_points; i++) {
        double curr_val = results[i];
        min_val = fmin(min_val, curr_val);
        max_val = fmax(max_val, curr_val);
        
        if(i > 1) {
            double curr_diff = curr_val - results[i-1];
            // Проверка монотонности
            if((curr_diff > 0 && prev_diff < 0) || (curr_diff < 0 && prev_diff > 0)) {
                is_monotonic = 0;
                inflection_points++;
            }
            prev_diff = curr_diff;
        }
    }
    
    // 4. Вычисление оценок
    double value_range = max_val - min_val;
    
    // Стабильность: учитываем диапазон значений и количество точек перегиба
    double stability_score = (value_range < 1000.0 ? 1.0 : 0.5) * exp(-inflection_points / 5.0);
    
    // Новизна: пока все формулы считаем новыми
    double novelty_score = 1.0;
    
    // Паттерны: учитываем монотонность и периодичность
    double pattern_score = is_monotonic ? 1.0 : 0.5;
    
    // Эффективность: оцениваем время вычисления
    double efficiency_score = eval_time < 0.001 ? 1.0 : 0.5;
    
    // Итоговая оценка
    return complexity_weight * complexity_score +
           novelty_weight * novelty_score +
           stability_weight * stability_score +
           pattern_weight * pattern_score +
           efficiency_weight * efficiency_score;
}

// Сериализация формулы в JSON
char* serialize_formula(const Formula* formula) {
    if (!formula) return NULL;
    
    struct json_object *jobj = json_object_new_object();
    json_object_object_add(jobj, "id", json_object_new_string(formula->id));
    json_object_object_add(jobj, "content", json_object_new_string(formula->content));
    json_object_object_add(jobj, "effectiveness", json_object_new_double(formula->effectiveness));
    json_object_object_add(jobj, "created_at", json_object_new_int64(formula->created_at));
    json_object_object_add(jobj, "tests_passed", json_object_new_int(formula->tests_passed));
    json_object_object_add(jobj, "confirmations", json_object_new_int(formula->confirmations));
    
    const char* json_str = json_object_to_json_string(jobj);
    char* result = strdup(json_str);
    json_object_put(jobj);
    
    return result;
}

// Десериализация формулы из JSON
Formula* deserialize_formula(const char* json_str) {
    if (!json_str) return NULL;
    
    struct json_object *jobj = json_tokener_parse(json_str);
    if (!jobj) return NULL;
    
    Formula* formula = malloc(sizeof(Formula));
    if (!formula) {
        json_object_put(jobj);
        return NULL;
    }
    
    struct json_object *id_obj, *content_obj, *effectiveness_obj,
                      *created_at_obj, *tests_passed_obj, *confirmations_obj;
                      
    if (!json_object_object_get_ex(jobj, "id", &id_obj) ||
        !json_object_object_get_ex(jobj, "content", &content_obj) ||
        !json_object_object_get_ex(jobj, "effectiveness", &effectiveness_obj) ||
        !json_object_object_get_ex(jobj, "created_at", &created_at_obj) ||
        !json_object_object_get_ex(jobj, "tests_passed", &tests_passed_obj) ||
        !json_object_object_get_ex(jobj, "confirmations", &confirmations_obj)) {
        
        free(formula);
        json_object_put(jobj);
        return NULL;
    }
    
    strncpy(formula->id, json_object_get_string(id_obj), sizeof(formula->id) - 1);
    formula->id[sizeof(formula->id) - 1] = '\0';
    
    strncpy(formula->content, json_object_get_string(content_obj), sizeof(formula->content) - 1);
    formula->content[sizeof(formula->content) - 1] = '\0';
    
    formula->effectiveness = json_object_get_double(effectiveness_obj);
    formula->created_at = json_object_get_int64(created_at_obj);
    formula->tests_passed = json_object_get_int(tests_passed_obj);
    formula->confirmations = json_object_get_int(confirmations_obj);
    
    json_object_put(jobj);
    return formula;
}

// Функция для динамического изменения сложности формул
static int calculate_dynamic_complexity(int base_complexity, int iteration) {
    return base_complexity + (iteration % 5); // Пример: сложность увеличивается каждые 5 итераций
}

// Добавление итерации для динамической сложности
void example_dynamic_complexity() {
    static int iteration = 0; // Локальная статическая переменная для отслеживания итераций
    iteration++;
    int dynamic_complexity = calculate_dynamic_complexity(FORMULA_TYPE_SIMPLE, iteration);
    // ...дальнейшая обработка dynamic_complexity...
}
