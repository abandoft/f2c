#ifndef F2C_FRONTEND_PREPROCESSOR_PRIVATE_H
#define F2C_FRONTEND_PREPROCESSOR_PRIVATE_H

#include "frontend/preprocessor.h"

typedef struct PreprocessorMacroParameter {
    char *name;
    size_t name_length;
} PreprocessorMacroParameter;

typedef struct PreprocessorMacro {
    char *name;
    size_t name_length;
    char *value;
    size_t value_length;
    PreprocessorMacroParameter *parameters;
    size_t parameter_count;
    int function_like;
    int variadic;
    const char *definition_source_name;
    size_t definition_line;
    size_t definition_column;
} PreprocessorMacro;

typedef struct PreprocessorMacroArgument {
    const char *text;
    size_t length;
    F2cSourcePosition origin;
} PreprocessorMacroArgument;

typedef struct ConditionalFrame {
    const char *opening_source_name;
    size_t opening_line;
    int parent_active;
    int branch_taken;
    int else_seen;
} ConditionalFrame;

typedef struct Preprocessor {
    Context *context;
    PreprocessorMacro *macros;
    size_t macro_count;
    size_t macro_capacity;
    ConditionalFrame *conditionals;
    size_t conditional_count;
    size_t conditional_capacity;
    size_t conditional_base;
    const char *current_source_name;
    size_t current_line;
    const char *pending_source_name;
    size_t pending_line;
    int location_pending;
    Buffer *output;
    F2cSourceMap *output_source_map;
    F2cSourceForm source_form;
    size_t include_depth;
    const struct PreprocessorIncludeFrame *include_parent;
    char continued_character_quote;
    size_t continued_hollerith_characters;
    int active;
} Preprocessor;

typedef struct PreprocessorIncludeFrame {
    const char *source_name;
    const struct PreprocessorIncludeFrame *parent;
} PreprocessorIncludeFrame;

int f2c_preprocessor_evaluate_condition(Preprocessor *preprocessor, const char *text, size_t line,
                                        size_t column, int evaluate, int *condition);
void f2c_preprocessor_discard_macros(Preprocessor *preprocessor);
size_t f2c_preprocessor_find_macro(const Preprocessor *preprocessor, const char *name,
                                   size_t length);
int f2c_preprocessor_define_object(Preprocessor *preprocessor, const char *name, size_t name_length,
                                   const char *value, size_t value_length, size_t line,
                                   size_t column, const char *definition_source_name,
                                   size_t definition_column, F2cDiagnosticCode invalid_code);
int f2c_preprocessor_process_define(Preprocessor *preprocessor, const char *rest, size_t line,
                                    size_t column);
void f2c_preprocessor_undefine(Preprocessor *preprocessor, const char *name, size_t length);
int f2c_preprocessor_replacement_has_operator(const PreprocessorMacro *macro);
int f2c_preprocessor_build_operator_replacement(Preprocessor *preprocessor,
                                                const PreprocessorMacro *macro,
                                                const PreprocessorMacroArgument *arguments,
                                                size_t argument_count,
                                                F2cSourcePosition diagnostic_origin,
                                                Buffer *output);
int f2c_preprocessor_append(Preprocessor *preprocessor, Buffer *output, F2cSourceMap *source_map,
                            const char *text, size_t length, F2cSourcePosition expansion,
                            size_t expansion_width, unsigned char expansion_column_step,
                            F2cSourcePosition spelling, size_t spelling_width,
                            unsigned char spelling_column_step, int has_spelling);
int f2c_preprocessor_expand_source_line(Preprocessor *preprocessor, const char *text, size_t length,
                                        size_t line, F2cSourceForm form, Buffer *output,
                                        F2cSourceMap *source_map);
int f2c_preprocessor_expand_directive_operand(Preprocessor *preprocessor, const char *text,
                                              size_t length, size_t line, size_t column,
                                              Buffer *output);
int f2c_preprocessor_process_buffer(Preprocessor *preprocessor, const char *source, size_t length,
                                    F2cSourceForm form, const char *source_name, size_t depth,
                                    const PreprocessorIncludeFrame *include_parent, Buffer *output,
                                    F2cSourceMap *source_map);
int f2c_preprocessor_process_include(Preprocessor *preprocessor, const char *rest, size_t line,
                                     size_t column);

#endif
