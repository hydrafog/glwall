#define _POSIX_C_SOURCE 200809L
#include "slang_process.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *builtins[] = {"Source",         "Original",
                                 "SourceSize",     "OriginalSize",
                                 "OutputSize",     "FinalViewportSize",
                                 "FrameCount",     "FrameTime",
                                 "FrameDirection", NULL};

static bool is_builtin(const char *name) {
    for (int i = 0; builtins[i]; i++) {
        if (strcmp(name, builtins[i]) == 0)
            return true;
    }
    return false;
}

struct replacement {
    size_t start;
    size_t len;
    char *text;
};

struct replacement_list {
    struct replacement *items;
    size_t count;
    size_t capacity;
};

static void add_replacement(struct replacement_list *list, size_t start, size_t len,
                            const char *text) {
    if (list->count >= list->capacity) {
        list->capacity = list->capacity == 0 ? 16 : list->capacity * 2;
        list->items = realloc(list->items, list->capacity * sizeof(struct replacement));
    }
    list->items[list->count].start = start;
    list->items[list->count].len = len;
    list->items[list->count].text = text ? strdup(text) : strdup("");
    list->count++;
}

static int compare_replacements_asc(const void *a, const void *b) {
    const struct replacement *ra = (const struct replacement *)a;
    const struct replacement *rb = (const struct replacement *)b;
    if (ra->start < rb->start)
        return -1;
    if (ra->start > rb->start)
        return 1;
    return 0;
}

static char *apply_replacements_asc(const char *src, struct replacement_list *list) {
    qsort(list->items, list->count, sizeof(struct replacement), compare_replacements_asc);

    size_t src_len = strlen(src);
    size_t new_len = src_len;

    for (size_t i = 0; i < list->count; i++) {
        new_len -= list->items[i].len;
        new_len += strlen(list->items[i].text);
    }

    char *out = malloc(new_len + 1);
    if (!out)
        return NULL;

    size_t current_pos = 0;
    char *out_ptr = out;

    for (size_t i = 0; i < list->count; i++) {
        size_t start = list->items[i].start;
        size_t len = list->items[i].len;
        const char *text = list->items[i].text;

        if (start > current_pos) {
            size_t copy_len = start - current_pos;
            memcpy(out_ptr, src + current_pos, copy_len);
            out_ptr += copy_len;
        }

        size_t text_len = strlen(text);
        memcpy(out_ptr, text, text_len);
        out_ptr += text_len;

        current_pos = start + len;
    }

    if (current_pos < src_len) {
        strcpy(out_ptr, src + current_pos);
    } else {
        *out_ptr = '\0';
    }

    return out;
}

static char *find_matching_brace(const char *start) {
    const char *p = start;
    int depth = 0;
    while (*p) {
        if (*p == '{')
            depth++;
        else if (*p == '}') {
            depth--;
            if (depth == 0)
                return (char *)p;
        }
        p++;
    }
    return NULL;
}

static void skip_whitespace(const char **p) {
    while (**p && isspace((unsigned char)**p))
        (*p)++;
}

static char *get_word(const char **p) {
    skip_whitespace(p);
    const char *start = *p;
    while (**p && (isalnum((unsigned char)**p) || **p == '_'))
        (*p)++;
    size_t len = *p - start;
    if (len == 0)
        return NULL;
    char *word = malloc(len + 1);
    memcpy(word, start, len);
    word[len] = '\0';
    return word;
}

static char *extract_fragment_shader(const char *src) {
    const char *frag_pragma = strstr(src, "#pragma stage fragment");
    if (!frag_pragma) {
        return strdup(src);
    }

    const char *first_pragma = strstr(src, "#pragma stage");
    size_t shared_len = first_pragma ? (size_t)(first_pragma - src) : 0;

    const char *frag_start = frag_pragma + strlen("#pragma stage fragment");
    const char *next_pragma = strstr(frag_start, "#pragma stage");
    size_t frag_len = next_pragma ? (size_t)(next_pragma - frag_start) : strlen(frag_start);

    char *out = malloc(shared_len + frag_len + 1);
    if (!out)
        return NULL;

    if (shared_len > 0) {
        memcpy(out, src, shared_len);
    }
    memcpy(out + shared_len, frag_start, frag_len);
    out[shared_len + frag_len] = '\0';

    return out;
}

char *slang_process_to_gl330(const char *raw_src) {
    char *src = extract_fragment_shader(raw_src);
    if (!src)
        return NULL;

    struct replacement_list list = {0};
    const char *p = src;

    while (*p) {
        const char *layout_start = strstr(p, "layout");
        if (!layout_start)
            break;

        p = layout_start + 6;
        skip_whitespace(&p);
        if (*p != '(')
            continue;

        const char *paren_end = strchr(p, ')');
        if (!paren_end)
            break;
        p = paren_end + 1;

        skip_whitespace(&p);

        if (strncmp(p, "uniform", 7) == 0) {
            const char *uniform_kw = p;
            p += 7;
            skip_whitespace(&p);

            if (*p == '{' ||
                (isalnum((unsigned char)*p) && strchr(p, '{') && strchr(p, '{') < strchr(p, ';'))) {

                char *block_name = NULL;
                if (*p != '{') {
                    block_name = get_word(&p);
                }

                skip_whitespace(&p);
                if (*p != '{') {
                    if (block_name)
                        free(block_name);
                    continue;
                }

                const char *block_start = p;
                char *block_end = find_matching_brace(block_start);
                if (!block_end) {
                    if (block_name)
                        free(block_name);
                    break;
                }

                p = block_end + 1;
                skip_whitespace(&p);

                char *inst_name = NULL;
                if (*p != ';') {
                    inst_name = get_word(&p);
                }

                char *new_decls = malloc(4096);
                new_decls[0] = '\0';

                const char *body = block_start + 1;
                const char *body_end = block_end;

                while (body < body_end) {
                    skip_whitespace(&body);
                    if (body >= body_end)
                        break;

                    const char *stmt_start = body;
                    const char *stmt_end = strchr(body, ';');
                    if (!stmt_end || stmt_end > body_end)
                        break;

                    size_t stmt_len = stmt_end - stmt_start;
                    char *stmt = malloc(stmt_len + 1);
                    memcpy(stmt, stmt_start, stmt_len);
                    stmt[stmt_len] = '\0';

                    char *name_start = strrchr(stmt, ' ');
                    if (!name_start)
                        name_start = stmt;
                    else
                        name_start++;

                    char *name = strdup(name_start);

                    char *clean_name = strdup(name);
                    char *bracket = strchr(clean_name, '[');
                    if (bracket)
                        *bracket = '\0';

                    if (!is_builtin(clean_name)) {
                        strcat(new_decls, "uniform ");
                        strcat(new_decls, stmt);
                        strcat(new_decls, ";\n");
                    }

                    free(name);
                    free(clean_name);
                    free(stmt);

                    body = stmt_end + 1;
                }

                size_t full_block_len = (p - layout_start);
                if (*p == ';') {
                    full_block_len++;
                    p++;
                }

                add_replacement(&list, layout_start - src, full_block_len, new_decls);
                free(new_decls);

                if (inst_name) {
                    const char *search = src;
                    char search_pattern[256];
                    snprintf(search_pattern, sizeof(search_pattern), "%s.", inst_name);

                    while ((search = strstr(search, search_pattern))) {
                        if (search > src &&
                            (isalnum((unsigned char)search[-1]) || search[-1] == '_')) {
                            search++;
                            continue;
                        }
                        add_replacement(&list, search - src, strlen(search_pattern), "");
                        search += strlen(search_pattern);
                    }
                    free(inst_name);
                }

                if (block_name)
                    free(block_name);

            } else {
                const char *stmt_end = strchr(p, ';');
                if (!stmt_end)
                    break;

                const char *n = stmt_end - 1;
                while (n > p && isspace((unsigned char)*n))
                    n--;
                const char *name_end = n + 1;
                while (n > p && (isalnum((unsigned char)*n) || *n == '_'))
                    n--;
                const char *name_start = n + 1;

                size_t name_len = name_end - name_start;
                char *name = malloc(name_len + 1);
                memcpy(name, name_start, name_len);
                name[name_len] = '\0';

                if (is_builtin(name)) {
                    add_replacement(&list, layout_start - src, (stmt_end - layout_start) + 1, "");
                } else {
                    add_replacement(&list, layout_start - src, (uniform_kw + 7) - layout_start,
                                    "uniform");
                }
                free(name);

                p = stmt_end + 1;
            }
        } else if (strncmp(p, "in", 2) == 0 && isspace((unsigned char)p[2])) {
            const char *stmt_end = strchr(p, ';');
            if (stmt_end) {
                const char *n = stmt_end - 1;
                while (n > p && isspace((unsigned char)*n))
                    n--;
                const char *name_end = n + 1;
                while (n > p && (isalnum((unsigned char)*n) || *n == '_'))
                    n--;
                const char *name_start = n + 1;

                size_t name_len = name_end - name_start;
                char *name = malloc(name_len + 1);
                memcpy(name, name_start, name_len);
                name[name_len] = '\0';

                if (strcmp(name, "vTexCoord") == 0) {
                    add_replacement(&list, layout_start - src, (stmt_end - layout_start) + 1, "");
                } else {
                    add_replacement(&list, layout_start - src, (p - layout_start), "");
                }
                free(name);
            }
        } else if (strncmp(p, "out", 3) == 0 && isspace((unsigned char)p[3])) {
            const char *stmt_end = strchr(p, ';');
            if (stmt_end) {
                const char *n = stmt_end - 1;
                while (n > p && isspace((unsigned char)*n))
                    n--;
                const char *name_end = n + 1;
                while (n > p && (isalnum((unsigned char)*n) || *n == '_'))
                    n--;
                const char *name_start = n + 1;

                size_t name_len = name_end - name_start;
                char *name = malloc(name_len + 1);
                memcpy(name, name_start, name_len);
                name[name_len] = '\0';

                if (strcmp(name, "FragColor") == 0) {
                    add_replacement(&list, layout_start - src, (stmt_end - layout_start) + 1, "");
                } else {
                    add_replacement(&list, layout_start - src, (p - layout_start), "");
                }
                free(name);
            }
        }
    }

    char *result = apply_replacements_asc(src, &list);
    free(src);

    for (size_t i = 0; i < list.count; i++) {
        free(list.items[i].text);
    }
    free(list.items);

    return result;
}
