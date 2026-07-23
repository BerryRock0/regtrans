#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>
#include <locale.h>
#include <wchar.h>

#define MAX_LINE 8192
#define MAX_MATCH 512

typedef struct
{
    char *input_file;
    char *output_file;
    char *pattern;
    char *source_lang;
    char *target_lang;
} Options;

Options parse_arguments(int argc, char *argv[])
{
    int flags;
    Options opts = {.input_file = NULL, .output_file = NULL, .source_lang = NULL, .target_lang = NULL, .pattern = NULL};

    while((flags = getopt(argc, argv, "i:o:s:t:p:E:l:h")) != -1)
    {
        switch (flags)
        {
            case 'i': opts.input_file = optarg; break;
            case 'o': opts.output_file = optarg; break;
            case 's': opts.source_lang = optarg; break;
            case 't': opts.target_lang = optarg; break;
            case 'p': opts.pattern = optarg; break;
            case 'E': setenv("LC_ALL", optarg, 1); break;
            case 'l': setenv("LANG", optarg, 1); break;
            case 'h': fprintf(stdout, "-i <input file> -o <output file> -p <regexp> -s <source lang> -t <target lang> -l <en>"); fflush(stdout);
            case '?': fprintf(stderr, "Unknown option\n"); exit(1);
        }        
    }

    return opts;
}

char* translate_via_trans(const char *text, const char *source, const char *target)
{
    static char result[MAX_LINE];
    memset(result, 0, sizeof(result));

    // Создаём временный файл для передачи текста
    FILE *temp = fopen("/tmp/trans_input.txt", "w");
    if (!temp)
    {
        perror("Ошибка создания временного файла");
        return NULL;
    }
    
    fprintf(temp, "%s", text);
    fclose(temp);

    char command[MAX_LINE];
    snprintf(command, sizeof(command), "trans -b %s:%s < /tmp/trans_input.txt 2>/dev/null", 
             source, target);

    FILE *pipe = popen(command, "r");
    if (!pipe)
    {
        perror("Ошибка при выполнении trans");
        return NULL;
    }

    if (fgets(result, sizeof(result), pipe) != NULL)
    {
        size_t len = strlen(result);
        if (len > 0 && result[len - 1] == '\n')
        {
            result[len - 1] = '\0';
        }
    }

    pclose(pipe);
    
    // Удаляем временный файл
    remove("/tmp/trans_input.txt");

    return result;
}

void process_file(Options opts)
{
    FILE *input = fopen(opts.input_file, "rb");
    FILE *output = fopen(opts.output_file, "wb");

    if (!input)
    {
        fprintf(stderr, "Ошибка: не удалось открыть входной файл %s\n", opts.input_file);
        exit(1);
    }

    if (!output)
    {
        fprintf(stderr, "Ошибка: не удалось открыть выходной файл %s\n", opts.output_file);
        fclose(input);
        exit(1);
    }

    regex_t regex;
    int reti = regcomp(&regex, opts.pattern, REG_EXTENDED);
    if (reti)
    {
        fprintf(stderr, "Ошибка компиляции регулярного выражения\n");
        fclose(input);
        fclose(output);
        exit(1);
    }

    unsigned char line[MAX_LINE];
    int line_num = 0;
    int translated_count = 0;

    while (fgets((char *)line, sizeof(line), input))
    {
        line_num++;
        unsigned char result_line[MAX_LINE * 3];
        result_line[0] = '\0';
        
        regmatch_t match;
        char *work_line = strdup((char *)line);
        char *current = work_line;

        while (regexec(&regex, current, 1, &match, 0) == 0)
        {
            // Добавляем текст до совпадения
            strncat((char *)result_line, current, match.rm_so);

            // Извлекаем найденный текст
            int match_len = match.rm_eo - match.rm_so;
            char matched_text[MAX_MATCH];
            strncpy(matched_text, current + match.rm_so, match_len);
            matched_text[match_len] = '\0';

            // Переводим найденный текст
            char *translated = translate_via_trans(matched_text, opts.source_lang, opts.target_lang);

            if (translated && strlen(translated) > 0)
            {
                strcat((char *)result_line, translated);
                fprintf(stdout, "Переведено: '%s' -> '%s'\n", matched_text, translated);
                fflush(stdout);
                translated_count++;
            }
            else
            {
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
}

int main(int argc, char *argv[])
{
    if (argc == 1)
    {
        print_usage(argv[0]);
        exit(1);
    }

    Options opts = parse_arguments(argc, argv);
    process_file(opts);

    return 0;
}
