#include "abstract_mind.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <time.h>

// Вспомогательные функции
static int is_number(const char* str) {
    while (*str) {
        if (!isdigit(*str) && *str != '.' && *str != '-')
            return 0;
        str++;
    }
    return 1;
}

static int evaluate_expression(const char* expr) {
    int result = 0;
    
    // Убираем пробелы
    char clean_expr[1024] = {0};
    int j = 0;
    for (int i = 0; expr[i]; i++) {
        if (!isspace(expr[i])) {
            clean_expr[j++] = expr[i];
        }
    }
    
    // Проверяем на простое число
    if (is_number(clean_expr)) {
        sscanf(clean_expr, "%d", &result);
        return result;
    }
    
        // Проверяем на выражение с оператором =
    char *eq_pos = strchr(clean_expr, '=');
    if (eq_pos) {
        *eq_pos = '\0';  // Разделяем на левую и правую части
        int left = evaluate_expression(clean_expr);
        int right = evaluate_expression(eq_pos + 1);
        return (left == right);
    }
    
    // Базовые математические операции
    char op = 0;
    char *op_pos = strpbrk(clean_expr, "+-*/");
    if (op_pos) {
        op = *op_pos;
        *op_pos = '\0';
        int left = evaluate_expression(clean_expr);
        int right = evaluate_expression(op_pos + 1);
        
        switch(op) {
            case '+': return left + right;
            case '-': return left - right;
            case '*': return left * right;
            case '/': return right != 0 ? left / right : 0;
            default: return 0;
        }
    }
    
    return result;
}

// Генерация начальной формулы
Formula generate_formula(void) {
    Formula formula = {0};
    formula.effectiveness = 0.5;  // Начальная эффективность 50%
    formula.timestamp = time(NULL);
    return formula;
}

Formula evolve_formula(Formula initial) {
    Formula evolved = initial;
    evolved.timestamp = time(NULL);
    
    // Если выражение пустое, возвращаем общий ответ
    if (!*initial.expression) {
        snprintf(evolved.expression, sizeof(evolved.expression),
                "Пожалуйста, введите задачу");
        evolved.effectiveness = 0.0;
        return evolved;
    }
    
    // Обработка специальных ключевых слов
    if (strstr(initial.expression, "привет")) {
        snprintf(evolved.expression, sizeof(evolved.expression),
                "Здравствуйте! Я готов помочь вам с решением задач.");
        evolved.effectiveness = 1.0;
        return evolved;
    }
    
    if (strstr(initial.expression, "серьезно")) {
        snprintf(evolved.expression, sizeof(evolved.expression),
                "Да, я отношусь к задачам со всей серьезностью!");
        evolved.effectiveness = 0.95;
        return evolved;
    }
    
    // Анализ математического выражения
    char clean_expr[1024] = {0};
    int j = 0;
    for (int i = 0; initial.expression[i]; i++) {
        if (!isspace(initial.expression[i])) {
            clean_expr[j++] = initial.expression[i];
        }
    }
    
    // Проверка на уравнение
    char *eq_pos = strchr(clean_expr, '=');
    if (eq_pos) {
        *eq_pos = '\0';
        int left = evaluate_expression(clean_expr);
        int right = evaluate_expression(eq_pos + 1);
        if (left == right) {
            snprintf(evolved.expression, sizeof(evolved.expression),
                    "Верно! %d = %d", left, right);
            evolved.effectiveness = 1.0;
        } else {
            snprintf(evolved.expression, sizeof(evolved.expression),
                    "Неверно! %d ≠ %d", left, right);
            evolved.effectiveness = 0.0;
        }
        return evolved;
    }
    
    // Проверка на математическое выражение
    int result = evaluate_expression(clean_expr);
    if (result || clean_expr[0] == '0') {
        snprintf(evolved.expression, sizeof(evolved.expression),
                "Результат вычисления: %d", result);
        evolved.effectiveness = 0.9;
        return evolved;
    }
    
    // Общий ответ для нераспознанных выражений
    snprintf(evolved.expression, sizeof(evolved.expression),
            "Я проанализировал ваш запрос \"%s\" и продолжаю учиться его обрабатывать",
            initial.expression);
    evolved.effectiveness = 0.3;
    
    return evolved;
}

// Абстракция формул
void create_abstraction(Formula* formulas, int count) {
    printf("Creating abstraction from %d formulas:\n", count);
    for (int i = 0; i < count; i++) {
        printf("  Formula %d: %s (Effectiveness: %.2lf)\n", i + 1, formulas[i].expression, formulas[i].effectiveness);
    }
}

// Самообучение
void self_learn(Formula* formulas, int count) {
    for (int i = 0; i < count; i++) {
        formulas[i] = evolve_formula(formulas[i]);
    }
}
