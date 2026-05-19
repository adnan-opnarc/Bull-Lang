#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "toml.h"

static char *trim(char *str) {
    while (*str == ' ' || *str == '\t') str++;
    char *end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) end--;
    *(end + 1) = '\0';
    return str;
}

static char *unquote(char *str) {
    char *start = strchr(str, '"');
    if (!start) return strdup(str);
    char *end = strrchr(start + 1, '"');
    if (!end) return strdup(start + 1);
    *end = '\0';
    return strdup(start + 1);
}

TomlDocument *toml_parse(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) return NULL;
    
    TomlDocument *doc = calloc(1, sizeof(TomlDocument));
    doc->section_capacity = 8;
    doc->sections = calloc(doc->section_capacity, sizeof(TomlSection));
    
    TomlSection *current = NULL;
    char line[512];
    
    while (fgets(line, sizeof(line), f)) {
        char *trimmed = trim(line);
        if (*trimmed == '#' || *trimmed == '\0') continue;
        
        if (*trimmed == '[') {
            char *end = strchr(trimmed, ']');
            if (end) {
                *end = '\0';
                current = NULL;
                for (int i = 0; i < doc->section_count; i++) {
                    if (strcmp(doc->sections[i].name, trimmed + 1) == 0) {
                        current = &doc->sections[i];
                        break;
                    }
                }
                if (!current) {
                    if (doc->section_count >= doc->section_capacity) {
                        doc->section_capacity *= 2;
                        doc->sections = realloc(doc->sections, doc->section_capacity * sizeof(TomlSection));
                    }
                    current = &doc->sections[doc->section_count++];
                    current->name = strdup(trimmed + 1);
                    current->pair_count = 0;
                    current->pair_capacity = 8;
                    current->pairs = calloc(current->pair_capacity, sizeof(TomlKeyValue));
                }
            }
        } else if (current) {
            char *eq = strchr(trimmed, '=');
            if (eq) {
                *eq = '\0';
                char *key = trim(trimmed);
                char *val = trim(eq + 1);
                
                if (current->pair_count >= current->pair_capacity) {
                    current->pair_capacity *= 2;
                    current->pairs = realloc(current->pairs, current->pair_capacity * sizeof(TomlKeyValue));
                }
                current->pairs[current->pair_count].key = strdup(key);
                current->pairs[current->pair_count].value = unquote(val);
                current->pair_count++;
            }
        }
    }
    fclose(f);
    return doc;
}

void toml_free(TomlDocument *doc) {
    if (!doc) return;
    for (int i = 0; i < doc->section_count; i++) {
        free(doc->sections[i].name);
        for (int j = 0; j < doc->sections[i].pair_count; j++) {
            free(doc->sections[i].pairs[j].key);
            free(doc->sections[i].pairs[j].value);
        }
        free(doc->sections[i].pairs);
    }
    free(doc->sections);
    free(doc);
}

const char *toml_get_string(TomlDocument *doc, const char *section, const char *key) {
    for (int i = 0; i < doc->section_count; i++) {
        if (strcmp(doc->sections[i].name, section) == 0) {
            for (int j = 0; j < doc->sections[i].pair_count; j++) {
                if (strcmp(doc->sections[i].pairs[j].key, key) == 0) {
                    return doc->sections[i].pairs[j].value;
                }
            }
        }
    }
    return NULL;
}

int toml_get_string_array(TomlDocument *doc, const char *section, const char **out_array, int max_items) {
    int count = 0;
    for (int i = 0; i < doc->section_count; i++) {
        if (strcmp(doc->sections[i].name, section) == 0) {
            for (int j = 0; j < doc->sections[i].pair_count && count < max_items; j++) {
                out_array[count++] = doc->sections[i].pairs[j].value;
            }
        }
    }
    return count;
}
