#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>
#include <locale.h>
#include <wchar.h>

#define MAX_LINE 8192
#define MAX_MATCH 512

typedef struct {
    char *input_file;
    char *output_file;
    char *pattern;
    char *source_lang;
    char *target_lang;
} Options;

void print_usage(const char *program_name) {
    fprintf(stdout, "Использование: %s [параметры]\n", program_name);
    fprintf(stdout, "Параметры:\n");
    fprintf(stdout, "  -i <файл>        Входной файл (обязательный)\n");
    fprintf(stdout, "  -o <файл>        Выходной файл (обязательный)\n");
    fprintf(stdout, "  -p <регулярное выражение>  Регулярное выражение для поиска (обязательное)\n");
    fprintf(stdout, "  -s <язык>        Исходный язык (по умолчанию: en)\n");
    fprintf(stdout, "  -t <язык>        Целевой язык (по умолчанию: ru)\n");
    fflush(stdout);
}

Options parse_arguments(int argc, char *argv[]) {
    Options opts = {
        .input_file = NULL,
        .output_file = NULL,
        .pattern = NULL,
        .source_lang = "en",
        .target_lang = "ru"
    };

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            opts.input_file = argv[++i];
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            opts.output_file = argv[++i];
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            opts.pattern = argv[++i];
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            opts.source_lang = argv[++i];
        } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            opts.target_lang = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            exit(0);
        }
    }

    if (!opts.input_file || !opts.output_file || !opts.pattern) {
        fprintf(stderr, "Ошибка: обязательные параметры -i, -o и -p не установлены\n");
        print_usage(argv[0]);
        exit(1);
    }

    return opts;
}

char* translate_via_trans(const char *text, const char *source, const char *target) {
    static char result[MAX_LINE];
    memset(result, 0, sizeof(result));

    // Создаём временный файл для передачи текста
    FILE *temp = fopen("/tmp/trans_input.txt", "w");
    if (!temp) {
        perror("Ошибка создания временного файла");
        return NULL;
    }
    
    fprintf(temp, "%s", text);
    fclose(temp);

    char command[MAX_LINE];
    snprintf(command, sizeof(command), "trans -b %s:%s < /tmp/trans_input.txt 2>/dev/null", 
             source, target);

    FILE *pipe = popen(command, "r");
    if (!pipe) {
        perror("Ошибка при выполнении trans");
        return NULL;
    }

    if (fgets(result, sizeof(result), pipe) != NULL) {
        size_t len = strlen(result);
        if (len > 0 && result[len - 1] == '\n') {
            result[len - 1] = '\0';
        }
    }

    pclose(pipe);
    
    // Удаляем временный файл
    remove("/tmp/trans_input.txt");

    return result;
}

void process_file(Options opts) {
    FILE *input = fopen(opts.input_file, "rb");
    FILE *output = fopen(opts.output_file, "wb");

    if (!input) {
        fprintf(stderr, "Ошибка: не удалось открыть входной файл %s\n", opts.input_file);
        exit(1);
    }

    if (!output) {
        fprintf(stderr, "Ошибка: не удалось открыть выходной файл %s\n", opts.output_file);
        fclose(input);
        exit(1);
    }

    regex_t regex;
    int reti = regcomp(&regex, opts.pattern, REG_EXTENDED);
    if (reti) {
        fprintf(stderr, "Ошибка компиляции регулярного выражения\n");
        fclose(input);
        fclose(output);
        exit(1);
    }

    unsigned char line[MAX_LINE];
    int line_num = 0;
    int translated_count = 0;

    while (fgets((char *)line, sizeof(line), input)) {
        line_num++;
        unsigned char result_line[MAX_LINE * 3];
        result_line[0] = '\0';
        
        regmatch_t match;
        char *work_line = strdup((char *)line);
        char *current = work_line;

        while (regexec(&regex, current, 1, &match, 0) == 0) {
            // Добавляем текст до совпадения
            strncat((char *)result_line, current, match.rm_so);

            // Извлекаем найденный текст
            int match_len = match.rm_eo - match.rm_so;
            char matched_text[MAX_MATCH];
            strncpy(matched_text, current + match.rm_so, match_len);
            matched_text[match_len] = '\0';

            // Переводим найденный текст
            char *translated = translate_via_trans(matched_text, 
                                                   opts.source_lang, 
                                                   opts.target_lang);

            if (translated && strlen(translated) > 0) {
                strcat((char *)result_line, translated);
                fprintf(stdout, "Переведено: '%s' -> '%s'\n", matched_text, translated);
                fflush(stdout);
                translated_count++;
            } else {
                strcat((char *)result_line, matched_text);
            }

            // Переходим к следующей части
            current += match.rm_eo;
        }

        // Добавляем оставшуюся часть строки
        strcat((char *)result_line, current);
        fprintf(output, "%s", (char *)result_line);

        free(work_line);
    }

    regfree(&regex);
    fclose(input);
    fclose(output);

    fprintf(stdout, "\n=== Результаты обработки ===\n");
    fprintf(stdout, "Входной файл: %s\n", opts.input_file);
    fprintf(stdout, "Выходной файл: %s\n", opts.output_file);
    fprintf(stdout, "Обработано строк: %d\n", line_num);
    fprintf(stdout, "Переведено слов: %d\n", translated_count);
    fflush(stdout);
}

int main(int argc, char *argv[]) {
    // Устанавливаем локаль
    setlocale(LC_ALL, "");
    
    // Переустанавливаем переменные окружения для UTF-8
    setenv("LANG", "en_US.UTF-8", 1);
    setenv("LC_ALL", "en_US.UTF-8", 1);

    if (argc == 1) {
        print_usage(argv[0]);
        exit(1);
    }

    Options opts = parse_arguments(argc, argv);

    fprintf(stdout, "=== Параметры ===\n");
    fprintf(stdout, "Входной файл: %s\n", opts.input_file);
    fprintf(stdout, "Выходной файл: %s\n", opts.output_file);
    fprintf(stdout, "Регулярное выражение: %s\n", opts.pattern);
    fprintf(stdout, "Язык источника: %s\n", opts.source_lang);
    fprintf(stdout, "Целевой язык: %s\n\n", opts.target_lang);
    fflush(stdout);

    process_file(opts);

    return 0;
}
