#ifndef TOML_H
#define TOML_H

typedef struct {
    char *key;
    char *value;
} TomlKeyValue;

typedef struct {
    char *name;
    TomlKeyValue *pairs;
    int pair_count;
    int pair_capacity;
} TomlSection;

typedef struct {
    TomlSection *sections;
    int section_count;
    int section_capacity;
} TomlDocument;

TomlDocument *toml_parse(const char *filename);
void toml_free(TomlDocument *doc);

const char *toml_get_string(TomlDocument *doc, const char *section, const char *key);
int toml_get_string_array(TomlDocument *doc, const char *section, const char **out_array, int max_items);

#endif
