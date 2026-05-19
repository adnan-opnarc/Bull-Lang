/*
 * Bull Compiler - Main Driver
 * The Bull compiler (bullc) - compiles .bl files to LLVM bitcode
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "lexer.h"
#include "parser.h"
#include "codegen.h"
#include "toml.h"
#include "error.h"
#include "color.h"

static int dir_exists(const char *path);
static char *crate_name_from_path(const char *path);
static char **extract_srcs_from_config(const char *config_file, int *count, int **is_other);
static void print_help(const char *prog_name);
static void print_version(void);
static int validate_crates(const char *source, const char *primary_file, const char *config_dir);
static void load_all_crate_syntax(const char *source, const char *config_dir);

static int dir_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

static char *read_file(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " Could not open file '%s'\n", filename);
        return NULL;
    }
    
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *buf = malloc(len + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    
    fread(buf, 1, len, f);
    buf[len] = '\0';
    fclose(f);
    
    return buf;
}

static char *crate_name_from_path(const char *path) {
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    char *name = strdup(base);
    char *dot = strrchr(name, '.');
    if (dot) *dot = '\0';
    return name;
}

static char **extract_srcs_from_config(const char *config_file, int *count, int **is_other) {
    TomlDocument *doc = toml_parse(config_file);
    if (!doc) {
        fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " Cannot parse config: %s\n", config_file);
        *count = 0;
        return NULL;
    }
    
    char **src_files = NULL;
    int capacity = 0;
    int src_count = 0;
    
    for (int i = 0; i < doc->section_count; i++) {
        if (strcmp(doc->sections[i].name, "srcs") == 0) {
            capacity = doc->sections[i].pair_count * 4;
            src_files = calloc(capacity, sizeof(char *));
            *is_other = calloc(capacity, sizeof(int));
            
            for (int j = 0; j < doc->sections[i].pair_count; j++) {
                const char *key = doc->sections[i].pairs[j].key;
                const char *value = doc->sections[i].pairs[j].value;
                
                if (strcmp(key, "other") == 0) {
                    char *dup = strdup(value);
                    char *tok = strtok(dup, ",");
                    while (tok) {
                        while (*tok == ' ' || *tok == '\t') tok++;
                        size_t len = strlen(tok);
                        while (len > 0 && (tok[len-1] == ' ' || tok[len-1] == '\t')) len--;
                        if (len > 0) {
                            char *file = malloc(len + 1);
                            strncpy(file, tok, len);
                            file[len] = '\0';
                            src_files[src_count] = file;
                            (*is_other)[src_count] = 1;
                            src_count++;
                        }
                        tok = strtok(NULL, ",");
                    }
                    free(dup);
                } else {
                    src_files[src_count] = strdup(value);
                    (*is_other)[src_count] = 0;
                    src_count++;
                }
            }
            break;
        }
    }
    
    if (src_count == 0) {
        src_files = malloc(sizeof(char *));
        *is_other = calloc(1, sizeof(int));
        src_files[0] = strdup("src/main.bl");
        (*is_other)[0] = 0;
        src_count = 1;
    }
    
    toml_free(doc);
    *count = src_count;
    return src_files;
}

static void print_help(const char *prog_name) {
    fprintf(stderr, "Bull Compiler - v1.0.0\n");
    fprintf(stderr, "Usage: %s [options] <input>\n", prog_name);
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  -o <file>       Output file (default: out.bc)\n");
    fprintf(stderr, "  -obj            Output object file (.o)\n");
    fprintf(stderr, "  -exe,-bin,-elf  Output executable\n");
    fprintf(stderr, "  -ar             Output static library (.a)\n");
    fprintf(stderr, "  -r              Run the compiled executable after build\n");
    fprintf(stderr, "  -i              Install crates from [crates] before compile\n");
    fprintf(stderr, "  --help          Show this help message\n");
    fprintf(stderr, "  --version       Show version information\n");
}

static void print_version(void) {
    fprintf(stderr, "Bull Compiler version 1.0.0\n");
    fprintf(stderr, "Built with LLVM 22.1.4\n");
}

static int validate_crates(const char *source, const char *primary_file, const char *config_dir) {
    static const char *builtin_crates[] = { "standard", NULL };
    
    const char *p = source;
    int line_num = 1;
    const char *line_start = source;
    
    while (*p) {
        if (strncmp(p, "using ", 6) == 0) {
            const char *start = p + 6;
            while (*start == ' ' || *start == '\t') start++;
            const char *end = start;
            while (*end && *end != '\n' && *end != '\r' && *end != ' ') end++;
            int name_len = end - start;
            if (name_len > 0) {
                char *crate_name = malloc(name_len + 1);
                strncpy(crate_name, start, name_len);
                crate_name[name_len] = '\0';
                
                int is_builtin = 0;
                for (int i = 0; builtin_crates[i]; i++) {
                    if (strcmp(crate_name, builtin_crates[i]) == 0) {
                        is_builtin = 1;
                        break;
                    }
                }
                
                if (!is_builtin) {
                    char crate_path[512];
                    snprintf(crate_path, sizeof(crate_path), "%s/crates/%s", config_dir, crate_name);
                    
                    if (!dir_exists(crate_path)) {
                        int col = (start - line_start) + 1;
                        fprintf(stderr, "  [%d] %s (line,file_name)\n\n", line_num, primary_file);
                        fprintf(stderr, "  %d| using %s\n", line_num, crate_name);
                        int prefix_spaces = 2;
                        char line_buf[32];
                        snprintf(line_buf, sizeof(line_buf), "%d", line_num);
                        prefix_spaces += strlen(line_buf) + 2;
                        for (int i = 0; i < prefix_spaces - 1; i++) fputc(' ', stderr);
                        for (int i = 0; i < col; i++) fputc(' ', stderr);
                        fprintf(stderr, "^^^^ unidentified crate being used\n");
                        free(crate_name);
                        return 0;
                    }
                }
                free(crate_name);
            }
            p = end;
            line_start = p;
            continue;
        }
        if (*p == '\n') {
            line_num++;
            line_start = p + 1;
        }
        p++;
    }
    return 1;
}

static void load_all_crate_syntax(const char *source, const char *config_dir) {
    static const char *builtin_crates[] = { "standard", NULL };

    const char *p = source;
    while (*p) {
        if (strncmp(p, "using ", 6) == 0) {
            const char *start = p + 6;
            while (*start == ' ' || *start == '\t') start++;
            const char *end = start;
            while (*end && *end != '\n' && *end != '\r' && *end != ' ') end++;
            int name_len = end - start;
            if (name_len > 0) {
                char *crate_name = malloc(name_len + 1);
                strncpy(crate_name, start, name_len);
                crate_name[name_len] = '\0';

                int is_builtin = 0;
                for (int i = 0; builtin_crates[i]; i++) {
                    if (strcmp(crate_name, builtin_crates[i]) == 0) {
                        is_builtin = 1;
                        break;
                    }
                }

                if (!is_builtin) {
                    char config_path[512];
                    snprintf(config_path, sizeof(config_path), "%s/crates/%s/bullfc.toml", config_dir, crate_name);

                    TomlDocument *config_doc = toml_parse(config_path);
                    if (config_doc) {
                        const char *syntax_file = toml_get_string(config_doc, "crate", "syntax");
                        if (syntax_file) {
                            char syntax_path[512];
                            if (syntax_file[0] == '/') {
                                strcpy(syntax_path, syntax_file);
                            } else {
                                snprintf(syntax_path, sizeof(syntax_path), "%s/crates/%s/%s", config_dir, crate_name, syntax_file);
                            }

                            TomlDocument *syntax_doc = toml_parse(syntax_path);
                            if (syntax_doc) {
                                for (int i = 0; i < syntax_doc->section_count; i++) {
                                    if (strcmp(syntax_doc->sections[i].name, "syntax") == 0) {
                                        for (int j = 0; j < syntax_doc->sections[i].pair_count; j++) {
                                            const char *key = syntax_doc->sections[i].pairs[j].key;
                                            if (strncmp(key, "fn.", 3) == 0) {
                                                printf(COLOR_GREEN "  [syntax] %s.%s(%s)\n" COLOR_RESET,
                                                    crate_name, key + 3,
                                                    syntax_doc->sections[i].pairs[j].value);
                                            }
                                        }
                                        break;
                                    }
                                }
                                toml_free(syntax_doc);
                            }
                        }
                        toml_free(config_doc);
                    }
                }
                free(crate_name);
            }
            p = end;
            continue;
        }
        p++;
    }
}

int main(int argc, char **argv) {
    const char *input_file = NULL;
    const char *output_file = NULL;
    int flag_obj = 0;
    int flag_exe = 0;
    int flag_ar = 0;
    int flag_run = 0;
    int flag_install = 0;
    
    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            print_help(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "--version") == 0) {
            print_version();
            return 0;
        }
        if (strcmp(argv[i], "--") == 0) {
            continue;  // remaining args are positional (no leading dash check)
        }
        if (strcmp(argv[i], "-obj") == 0) {
            flag_obj = 1;
            continue;
        }
        if (strcmp(argv[i], "-r") == 0) {
            flag_run = 1;
            flag_exe = 1;
            continue;
        }
        if (strcmp(argv[i], "-i") == 0) {
            flag_install = 1;
            continue;
        }
        if (strcmp(argv[i], "-ar") == 0) {
            flag_ar = 1;
            flag_obj = 1;
            continue;
        }
        if (strcmp(argv[i], "-exe") == 0 || strcmp(argv[i], "-bin") == 0 || strcmp(argv[i], "-elf") == 0) {
            flag_exe = 1;
            continue;
        }
        if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 < argc) {
                output_file = argv[++i];
            } else {
                fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " -o requires a filename\n");
                return 1;
            }
            continue;
        }
        if (strncmp(argv[i], "-o", 2) == 0) {
            output_file = argv[i] + 2;
            continue;
        }
        if (argv[i][0] == '-') {
            fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " Unknown option '%s'\n", argv[i]);
            print_help(argv[0]);
            return 1;
        }
        if (!input_file) {
            input_file = argv[i];
        }
    }
    
    if (!input_file) {
        fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " No input file specified\n");
        print_help(argv[0]);
        return 1;
    }
    
    // Check if input is a config file
    char **source_files = NULL;
    int *is_other = NULL;
    int source_count = 0;
    char config_dir[512] = ".";
    
    if (strstr(input_file, "bullfc.toml") != NULL || strstr(input_file, ".toml") != NULL) {
        // Parse config to find source files
        source_files = extract_srcs_from_config(input_file, &source_count, &is_other);
        
        // Get config directory for relative paths
        strcpy(config_dir, input_file);
        char *last_slash = strrchr(config_dir, '/');
        if (last_slash) {
            *last_slash = '\0';
        } else {
            strcpy(config_dir, ".");
        }
        
        // Get output file from [build] section if not specified
        if (output_file == NULL) {
            TomlDocument *doc = toml_parse(input_file);
            if (doc) {
                const char *name_out = toml_get_string(doc, "build", "name.out");
                if (name_out) {
                    output_file = strdup(name_out);
                }
                toml_free(doc);
            }
        }
    } else {
        // Single source file
        source_files = malloc(sizeof(char *));
        source_files[0] = strdup(input_file);
        source_count = 1;
    }
    
    if (source_count == 0) {
        fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " No source files found\n");
        return 1;
    }
    
    if (output_file == NULL) {
        output_file = "out.bc";
    }
    
    // Read and combine all source files (two-pass for other files)
    char *combined_source = malloc(1);
    combined_source[0] = '\0';
    int total_len = 0;
    int loaded_count = 0;
    
    // Pass 1: load all non-other source files
    for (int i = 0; i < source_count; i++) {
        if (is_other && is_other[i]) continue;
        
        char full_path[512];
        if (source_files[i][0] == '/') {
            strcpy(full_path, source_files[i]);
        } else {
            snprintf(full_path, sizeof(full_path), "%s/%s", config_dir, source_files[i]);
        }
        
        char *src = read_file(full_path);
        if (!src) {
                fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " Could not read source file '%s'\n", full_path);
            return 1;
        }
        
        int src_len = strlen(src);
        combined_source = realloc(combined_source, total_len + src_len + 1);
        strcpy(combined_source + total_len, src);
        total_len += src_len;
        free(src);
        loaded_count++;
        
            printf(COLOR_GREEN "  [%d] loaded %s\n" COLOR_RESET, loaded_count, source_files[i]);
    }
    
    fflush(stdout);  // Ensure loaded messages appear before validation errors
    
    // Determine primary source filename for error reporting
    char *primary_filename = "main.bl";
    for (int i = 0; i < source_count; i++) {
        if (!is_other || !is_other[i]) {
            const char *path = source_files[i];
            const char *base = strrchr(path, '/');
            if (base) base++;
            else base = path;
            primary_filename = (char*)base;
            break;
        }
    }
    
    // Validate that all referenced crates exist
    if (!validate_crates(combined_source, primary_filename, config_dir)) {
        fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " Crate validation failed\n");
        free(combined_source);
        for (int i = 0; i < source_count; i++) free(source_files[i]);
        free(source_files);
        free(is_other);
        return 1;
    }
    
    // Load crate syntax from nesyntax.toml
    load_all_crate_syntax(combined_source, config_dir);
    
    // Pass 2: check which other files are referenced and load them
    if (is_other) {
        for (int i = 0; i < source_count; i++) {
            if (!is_other[i]) continue;
            
            char *crate = crate_name_from_path(source_files[i]);
            char pattern[256];
            snprintf(pattern, sizeof(pattern), "using %s", crate);
            
            int referenced = (strstr(combined_source, pattern) != NULL);
            free(crate);
            
            if (!referenced) {
                continue;
            }
            
            char full_path[512];
            if (source_files[i][0] == '/') {
                strcpy(full_path, source_files[i]);
            } else {
                snprintf(full_path, sizeof(full_path), "%s/%s", config_dir, source_files[i]);
            }
            
            char *src = read_file(full_path);
            if (!src) {
            fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " Could not read source file '%s'\n", full_path);
                return 1;
            }
            
            int src_len = strlen(src);
            combined_source = realloc(combined_source, total_len + src_len + 1);
            strcpy(combined_source + total_len, src);
            total_len += src_len;
            free(src);
            loaded_count++;
            
            printf(COLOR_GREEN "  [%d] loaded %s (referenced)\n" COLOR_RESET, loaded_count, source_files[i]);
        }
    }
    
    //printf("Loaded %d source file(s)\n", loaded_count);
    if (loaded_count == 0) {
        fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " No source files loaded\n");
        return 1;
    }
    
    // Auto-install crates if -i flag and using config
    if (flag_install && is_other) {
        printf(COLOR_GREEN "\ninstalling crates...\n" COLOR_RESET);
        int rc = system("bpkg install 2>&1");
        if (rc != 0) {
            rc = system("./bpkg install 2>&1");
        }
        if (rc != 0) {
             fprintf(stderr, COLOR_ORANGE "Warning:" COLOR_RESET " bpkg install failed (code %d)\n", rc);
        }
    }
    
    // Lexing
    // Determine primary source filename for error reporting
    char *display_filename = "main.bl";
    for (int i = 0; i < source_count; i++) {
        if (!is_other || !is_other[i]) {
            const char *path = source_files[i];
            const char *base = strrchr(path, '/');
            if (base) base++;
            else base = path;
            display_filename = (char*)base;
            break;
        }
    }
    
    Lexer *lexer = lexer_new(combined_source, display_filename);
    
    // Print file header
    printf("\n[1]%s-->\n", display_filename);
    fflush(stdout);
    
    // Validate tokens
    Token *tok = lexer_next_token(lexer);
    int token_count = 0;
    int had_error = 0;
    while (tok->type != TOKEN_EOF) {
        if (tok->type == TOKEN_ERROR) {
            report_lexer_error(lexer, tok, "unknown token");
            had_error = 1;
        }
        token_count++;
        Token *next = lexer_next_token(lexer);
        free(tok);
        tok = next;
    }
    free(tok);
    
    if (had_error) {
        fprintf(stderr, COLOR_RED "Lexing failed\n" COLOR_RESET);
        lexer_free(lexer);
        free(combined_source);
        for (int i = 0; i < source_count; i++) free(source_files[i]);
        free(source_files);
        free(is_other);
        return 1;
    }
    
    // Parsing
    lexer_free(lexer);
    lexer = lexer_new(combined_source, display_filename);
    
    Parser *parser = parser_new(lexer);
    ASTNode *ast = parser_parse(parser);
    
    if (!ast) {
        fprintf(stderr, COLOR_RED "Parsing failed\n" COLOR_RESET);
        parser_free(parser);
        lexer_free(lexer);
        free(combined_source);
        for (int i = 0; i < source_count; i++) free(source_files[i]);
        free(source_files);
        free(is_other);
        return 1;
    }
    //debug text for parser dont uncomment the line below
    //printf("Parsing complete\n");
    
     // Build IR and emit according to flags
      CodeGenContext *ctx = codegen_new();
      
      int gen_result = 0;
     
      // Build IR for object/executable/archive paths (bitcode path builds internally)
      if (flag_obj || flag_exe || flag_ar) {
          codegen_build_ir(ctx, ast);
      }
     
      if (flag_ar) {
          char obj_file[512];
          snprintf(obj_file, sizeof(obj_file), "%s.o", output_file);
          printf("[2/2] generating object file (%s)\n", obj_file);
          gen_result = codegen_emit_object(ctx, obj_file);
          if (gen_result != 0) {
              fprintf(stderr, COLOR_RED "Object file generation failed\n" COLOR_RESET);
          } else {
              char cmd[1024];
              snprintf(cmd, sizeof(cmd), "ar rcs \"%s\" \"%s\"", output_file, obj_file);
              printf("  generating archive (%s)\n", output_file);
              if (system(cmd) != 0) {
                  fprintf(stderr, COLOR_RED "Archive generation failed\n" COLOR_RESET);
                  gen_result = -1;
              } else {
                  printf(COLOR_GREEN "[2/2] finished\n" COLOR_RESET);
                  printf("         " COLOR_GREEN "↳(%s)\n" COLOR_RESET, output_file);
              }
              remove(obj_file);
          }
      } else if (flag_obj) {
          // Object file output
          printf("[2/2] generating object file (%s)\n", output_file);
          gen_result = codegen_emit_object(ctx, output_file);
          if (gen_result != 0) {
              fprintf(stderr, COLOR_RED "Object file generation failed\n" COLOR_RESET);
          } else {
              printf(COLOR_GREEN "[2/2] finished\n" COLOR_RESET);
              printf("         " COLOR_GREEN "↳(%s)\n" COLOR_RESET, output_file);
          }
      } else if (flag_exe) {
          printf("[2/2] generating executable (%s)\n", output_file);

          char obj_file[1024];
          snprintf(obj_file, sizeof(obj_file), "%s.o", output_file);
          gen_result = codegen_emit_object(ctx, obj_file);

          if (gen_result == 0) {
              char cmd[4096];
              int pos = snprintf(cmd, sizeof(cmd),
                  "gcc -no-pie -o \"%s\" \"%s\"", output_file, obj_file);

              char ls_cmd[1024];
              snprintf(ls_cmd, sizeof(ls_cmd),
                  "ls \"%s\"/crates/*/build/*.a 2>/dev/null | tr '\\n' ' '", config_dir);
              FILE *fp = popen(ls_cmd, "r");
              if (fp) {
                  char libs[2048];
                  if (fgets(libs, sizeof(libs), fp)) {
                      libs[strcspn(libs, "\n")] = '\0';
                      if (libs[0])
                          pos += snprintf(cmd + pos, sizeof(cmd) - pos,
                              " %s", libs);
                  }
                  pclose(fp);
              }

              snprintf(cmd + pos, sizeof(cmd) - pos, " 2>&1");
              printf("  linking...\n");

              if (system(cmd) != 0) {
                  fprintf(stderr, COLOR_RED "Linking failed\n" COLOR_RESET);
                  gen_result = -1;
              } else {
                  printf(COLOR_GREEN "[2/2] finished\n" COLOR_RESET);
                  printf("         " COLOR_GREEN "↳(%s)\n" COLOR_RESET, output_file);
              }
              remove(obj_file);
          } else {
              fprintf(stderr, COLOR_RED "Object file generation failed\n" COLOR_RESET);
          }
      } else {
         // Default: emit LLVM bitcode
         char bc_file[512];
         strcpy(bc_file, output_file);
         char *dot = strrchr(bc_file, '.');
         if (dot) {
             strcpy(dot, ".bc");
         } else {
             strcat(bc_file, ".bc");
         }
         gen_result = codegen_generate(ctx, ast, bc_file);
         if (gen_result == 0) {
             printf(COLOR_GREEN "[1/1] finished\n" COLOR_RESET);
             printf("         " COLOR_GREEN "↳(%s)\n" COLOR_RESET, bc_file);
         }
     }
     
      // Run the compiled binary if -r flag was set
      if (flag_run && gen_result == 0) {
          printf("\nrunning %s...\n", output_file);
          fflush(stdout);
          char run_cmd[1024];
          if (strchr(output_file, '/')) {
              snprintf(run_cmd, sizeof(run_cmd), "\"%s\"", output_file);
          } else {
              snprintf(run_cmd, sizeof(run_cmd), "./%s", output_file);
          }
          int run_rc = system(run_cmd);
          if (run_rc != 0) {
              fprintf(stderr, COLOR_ORANGE "Exit code: %d\n" COLOR_RESET, run_rc);
          }
      }

      // Cleanup
      codegen_free(ctx);
     parser_free(parser);
     lexer_free(lexer);
      free(combined_source);
      for (int i = 0; i < source_count; i++) free(source_files[i]);
      free(source_files);
      free(is_other);
      
      return gen_result == 0 ? 0 : 1;
}
