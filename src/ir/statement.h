#ifndef F2C_IR_STATEMENT_H
#define F2C_IR_STATEMENT_H

#include "ir/expression.h"

typedef enum F2cStatementKind {
    F2C_STMT_EMPTY,
    F2C_STMT_DECLARATION,
    F2C_STMT_END_SELECT,
    F2C_STMT_CASE,
    F2C_STMT_SELECT_CASE,
    F2C_STMT_TYPE_GUARD,
    F2C_STMT_SELECT_TYPE,
    F2C_STMT_BLOCK_SCOPE,
    F2C_STMT_END_BLOCK_SCOPE,
    F2C_STMT_END_BLOCK,
    F2C_STMT_ELSE_IF,
    F2C_STMT_ELSE,
    F2C_STMT_IF,
    F2C_STMT_ARITHMETIC_IF,
    F2C_STMT_DO_WHILE,
    F2C_STMT_DO,
    F2C_STMT_WRITE,
    F2C_STMT_READ,
    F2C_STMT_PRINT,
    F2C_STMT_OPEN,
    F2C_STMT_REWIND,
    F2C_STMT_CLOSE,
    F2C_STMT_ALLOCATE,
    F2C_STMT_DEALLOCATE,
    F2C_STMT_DATA,
    F2C_STMT_CALL,
    F2C_STMT_MOVE_ALLOC,
    F2C_STMT_NULLIFY,
    F2C_STMT_POINTER_ASSIGNMENT,
    F2C_STMT_RETURN,
    F2C_STMT_STOP,
    F2C_STMT_CYCLE,
    F2C_STMT_EXIT,
    F2C_STMT_CONTINUE,
    F2C_STMT_ASSIGN_LABEL,
    F2C_STMT_GOTO,
    F2C_STMT_ASSIGNED_GOTO,
    F2C_STMT_LABEL,
    F2C_STMT_ASSIGNMENT,
    F2C_STMT_UNSUPPORTED
} F2cStatementKind;

typedef enum F2cIrState { F2C_IR_SYNTAX, F2C_IR_TYPED } F2cIrState;

typedef struct F2cIoItem {
    char *text;
    F2cExpr *expression;
    struct F2cIoItem *children;
    size_t child_count;
    F2cExpr *iterator;
    F2cExpr *initial;
    F2cExpr *limit;
    F2cExpr *step;
    int implied_do;
} F2cIoItem;

typedef enum F2cIoControlKind {
    F2C_IO_CONTROL_POSITIONAL,
    F2C_IO_CONTROL_UNKNOWN,
    F2C_IO_CONTROL_UNIT,
    F2C_IO_CONTROL_FMT,
    F2C_IO_CONTROL_NML,
    F2C_IO_CONTROL_END,
    F2C_IO_CONTROL_EOR,
    F2C_IO_CONTROL_ERR,
    F2C_IO_CONTROL_IOSTAT,
    F2C_IO_CONTROL_IOMSG,
    F2C_IO_CONTROL_SIZE,
    F2C_IO_CONTROL_ADVANCE,
    F2C_IO_CONTROL_REC,
    F2C_IO_CONTROL_POS,
    F2C_IO_CONTROL_FILE,
    F2C_IO_CONTROL_STATUS,
    F2C_IO_CONTROL_ACCESS,
    F2C_IO_CONTROL_ACTION,
    F2C_IO_CONTROL_FORM,
    F2C_IO_CONTROL_RECL,
    F2C_IO_CONTROL_BLANK,
    F2C_IO_CONTROL_DECIMAL,
    F2C_IO_CONTROL_DELIM,
    F2C_IO_CONTROL_ENCODING,
    F2C_IO_CONTROL_PAD,
    F2C_IO_CONTROL_ROUND,
    F2C_IO_CONTROL_SIGN,
    F2C_IO_CONTROL_ASYNCHRONOUS,
    F2C_IO_CONTROL_ID,
    F2C_IO_CONTROL_NEWUNIT
} F2cIoControlKind;

typedef struct F2cIoControl {
    F2cIoControlKind kind;
    char *keyword;
    F2cExpr *value;
    int asterisk;
} F2cIoControl;

typedef struct F2cDataValue {
    char *text;
    F2cExpr *expression;
    F2cExpr *repeat;
} F2cDataValue;

typedef struct F2cDataGroup {
    F2cIoItem *targets;
    size_t target_count;
    F2cDataValue *values;
    size_t value_count;
} F2cDataGroup;

struct F2cStatement {
    F2cStatementKind kind;
    F2cIrState state;
    size_t line;
    F2cSourceSpan span;
    char *text;
    char *tail;
    char *name;
    char **items;
    F2cExpr **arguments;
    size_t item_count;
    F2cIoControl *io_controls;
    size_t control_count;
    F2cIoItem *io_items;
    size_t io_item_count;
    F2cDataGroup *data_groups;
    size_t data_group_count;
    F2cExpr *expression;
    F2cExpr *left;
    F2cExpr *right;
    F2cExpr *limit;
    F2cExpr *step;
    F2cExpr *allocation_character_length;
    F2cDerivedType *guard_type;
    char **labels;
    size_t label_count;
    F2cStatement *nested;
    int block;
    int error_stop;
    int unroll_hint;
};

char *f2c_find_assignment(char *line);
int f2c_parse_statement(Unit *unit, const char *text, size_t line, F2cStatement *statement);
int f2c_parse_statement_tokens(Unit *unit, const Line *line, F2cStatement *statement);
void f2c_statement_free(F2cStatement *statement);
void f2c_visit_statement_expressions(F2cStatement *statement, F2cExpressionVisitor visitor,
                                     void *state);

#endif
