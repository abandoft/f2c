#include "internal/f2c.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void expect(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

static void add_symbol(Symbol *symbol, const char *name, Type type) {
    memset(symbol, 0, sizeof(*symbol));
    symbol->name = (char *)name;
    symbol->c_name = (char *)name;
    symbol->type = type;
}

int main(void) {
    Symbol symbols[5];
    Unit unit;
    F2cStatement statement;
    memset(&unit, 0, sizeof(unit));
    add_symbol(&symbols[0], "x", TYPE_REAL);
    add_symbol(&symbols[1], "n", TYPE_INTEGER);
    add_symbol(&symbols[2], "done", TYPE_LOGICAL);
    add_symbol(&symbols[3], "work", TYPE_REAL);
    symbols[3].rank = 2U;
    symbols[3].allocatable = 1;
    add_symbol(&symbols[4], "values", TYPE_CHARACTER);
    symbols[4].rank = 1U;
    symbols[4].allocatable = 1;
    symbols[4].deferred_character = 1;
    unit.symbols = symbols;
    unit.symbol_count = 5U;

    {
        static char source[] = "x = x + 1.0";
        F2cToken tokens[8];
        F2cTokenStream stream;
        Line line = {0};
        size_t count = 0U;
        f2c_token_stream_init(&stream, source, 11U, 3U);
        for (;;) {
            f2c_token_stream_next(&stream);
            if (stream.token.kind == F2C_TOKEN_END)
                break;
            if (count < sizeof(tokens) / sizeof(tokens[0]))
                tokens[count++] = stream.token;
        }
        line.text = source;
        line.source_name = "token-path.f90";
        line.number = 11U;
        line.tokens = tokens;
        line.token_count = count;
        expect(f2c_parse_statement_tokens(&unit, &line, &statement),
               "statement syntax AST consumes the canonical line token stream");
        expect(statement.state == F2C_IR_SYNTAX && statement.span.begin.line == 11U &&
                   statement.span.begin.column == 3U &&
                   strcmp(statement.span.begin.source_name, "token-path.f90") == 0,
               "statement syntax AST retains phase and source span metadata");
        f2c_statement_free(&statement);
    }

    expect(f2c_parse_statement(&unit, "if (n.gt.0) x = 1.0", 12U, &statement),
           "single-line IF parses");
    expect(statement.kind == F2C_STMT_IF && statement.expression != NULL,
           "IF owns its condition AST");
    expect(statement.tail != NULL && strcmp(statement.tail, "x = 1.0") == 0,
           "single-line IF preserves its nested statement payload");
    expect(!statement.block && statement.nested != NULL &&
               statement.nested->kind == F2C_STMT_ASSIGNMENT,
           "single-line IF owns its nested statement IR");
    f2c_statement_free(&statement);

    expect(f2c_parse_statement(&unit, "block", 15U, &statement), "BLOCK parses");
    expect(statement.kind == F2C_STMT_BLOCK_SCOPE,
           "BLOCK owns a dedicated construct-scope statement kind");
    f2c_statement_free(&statement);

    expect(f2c_parse_statement(&unit, "block(1, 1) = value", 15U, &statement),
           "array named BLOCK parses");
    expect(statement.kind == F2C_STMT_ASSIGNMENT,
           "array named BLOCK is not confused with a BLOCK construct");
    f2c_statement_free(&statement);

    expect(f2c_parse_statement(&unit, "end block", 16U, &statement), "END BLOCK parses");
    expect(statement.kind == F2C_STMT_END_BLOCK_SCOPE,
           "END BLOCK owns a dedicated construct-scope terminator kind");
    f2c_statement_free(&statement);

    expect(f2c_parse_statement(&unit, "end if", 16U, &statement), "END IF parses");
    expect(statement.kind == F2C_STMT_END_IF, "END IF owns a distinct typed-IR terminator kind");
    f2c_statement_free(&statement);

    expect(f2c_parse_statement(&unit, "end do", 16U, &statement), "END DO parses");
    expect(statement.kind == F2C_STMT_END_DO, "END DO owns a distinct typed-IR terminator kind");
    f2c_statement_free(&statement);

    expect(f2c_parse_statement(&unit, "if (x) 10, 20, 30", 12U, &statement),
           "arithmetic IF parses");
    expect(statement.kind == F2C_STMT_ARITHMETIC_IF && statement.expression != NULL &&
               statement.label_count == 3U && strcmp(statement.labels[0], "10") == 0 &&
               strcmp(statement.labels[2], "30") == 0,
           "arithmetic IF owns its expression and three branch labels");
    f2c_statement_free(&statement);

    expect(f2c_parse_statement(&unit, "x = x + 1.0", 13U, &statement), "assignment parses");
    expect(statement.kind == F2C_STMT_ASSIGNMENT && statement.left != NULL &&
               statement.right != NULL,
           "assignment owns typed left and right expression ASTs");
    f2c_statement_free(&statement);

    expect(f2c_parse_statement(&unit, "do while (.not.done)", 14U, &statement), "DO WHILE parses");
    expect(statement.kind == F2C_STMT_DO_WHILE && statement.expression != NULL &&
               statement.expression->type == TYPE_LOGICAL,
           "DO WHILE condition has LOGICAL type");
    f2c_statement_free(&statement);

    expect(f2c_parse_statement(&unit, "do n = 1, 10, 2", 15U, &statement), "counted DO parses");
    expect(statement.kind == F2C_STMT_DO && statement.left != NULL && statement.right != NULL &&
               statement.limit != NULL && statement.step != NULL,
           "counted DO owns variable, initial, limit, and step ASTs");
    f2c_statement_free(&statement);

    expect(f2c_parse_statement(&unit, "outer: do n = 1, 10, 2", 15U, &statement),
           "named counted DO parses");
    expect(statement.kind == F2C_STMT_DO && statement.construct_syntax_valid &&
               statement.construct_name != NULL && strcmp(statement.construct_name, "outer") == 0 &&
               statement.left != NULL && statement.right != NULL && statement.limit != NULL &&
               statement.step != NULL,
           "named DO retains its construct identity and typed loop control AST");
    f2c_statement_free(&statement);

    expect(f2c_parse_statement(&unit, "decision: if (n > 0) then", 15U, &statement),
           "named block IF parses");
    expect(statement.kind == F2C_STMT_IF && statement.block && statement.construct_name != NULL &&
               strcmp(statement.construct_name, "decision") == 0 && statement.expression != NULL,
           "named IF classifies from the canonical token body without losing its prefix");
    expect(statement.expression != NULL && statement.expression->span.begin.line == 15U &&
               statement.expression->span.begin.column == 15U,
           "named construct expressions preserve their physical canonical-token span");
    f2c_statement_free(&statement);

    expect(f2c_parse_statement(&unit, "cycle outer", 15U, &statement), "named CYCLE parses");
    expect(statement.kind == F2C_STMT_CYCLE && statement.construct_syntax_valid &&
               statement.control_name != NULL && strcmp(statement.control_name, "outer") == 0 &&
               statement.control_target == NULL,
           "CYCLE owns an unresolved control name before semantic construct binding");
    f2c_statement_free(&statement);

    expect(f2c_parse_statement(&unit, "end do outer", 15U, &statement), "named END DO parses");
    expect(statement.kind == F2C_STMT_END_DO && statement.construct_name != NULL &&
               strcmp(statement.construct_name, "outer") == 0 && statement.name == NULL,
           "named END DO separates construct identity from legacy terminal labels");
    f2c_statement_free(&statement);

    expect(f2c_parse_statement(&unit, "end do 100", 15U, &statement),
           "legacy labeled END DO parses");
    expect(statement.kind == F2C_STMT_END_DO && statement.construct_name == NULL &&
               statement.name != NULL && strcmp(statement.name, "100") == 0,
           "numeric END DO suffix remains a legacy terminal label");
    f2c_statement_free(&statement);

    expect(f2c_parse_statement(&unit, "cycle outer extra", 15U, &statement),
           "malformed named CYCLE still builds syntax IR");
    expect(statement.kind == F2C_STMT_CYCLE && !statement.construct_syntax_valid,
           "malformed CYCLE target syntax is preserved for deterministic validation");
    f2c_statement_free(&statement);

    expect(f2c_parse_statement(&unit, "a:1:n)=a(n:1:-1)", 15U, &statement),
           "malformed construct-prefix input builds bounded syntax IR");
    expect(statement.kind == F2C_STMT_INVALID && !statement.construct_syntax_valid,
           "a numeric token after an identifier colon cannot recursively become a label");
    f2c_statement_free(&statement);

    expect(f2c_parse_statement(&unit, "do n = ((n - 1) / 4) * 4 + 1, 1, -4", 15U, &statement),
           "counted DO with a parenthesized initial expression parses");
    expect(statement.kind == F2C_STMT_DO && statement.left != NULL && statement.right != NULL &&
               statement.limit != NULL && statement.step != NULL,
           "top-level comma splitting does not mistake the initial parenthesis for a call list");
    f2c_statement_free(&statement);

    expect(f2c_parse_statement(&unit, "case (1, 3:5, :0, 9:)", 16U, &statement),
           "CASE value ranges parse");
    expect(statement.kind == F2C_STMT_CASE && statement.case_syntax_valid &&
               !statement.case_default && statement.case_range_count == 4U &&
               !statement.case_ranges[0].has_colon && statement.case_ranges[0].lower != NULL &&
               statement.case_ranges[0].upper == NULL && statement.case_ranges[1].has_colon &&
               statement.case_ranges[1].lower != NULL && statement.case_ranges[1].upper != NULL &&
               statement.case_ranges[2].has_colon && statement.case_ranges[2].lower == NULL &&
               statement.case_ranges[2].upper != NULL && statement.case_ranges[3].has_colon &&
               statement.case_ranges[3].lower != NULL && statement.case_ranges[3].upper == NULL,
           "CASE owns singleton, closed, lower-open, and upper-open range AST nodes");
    f2c_statement_free(&statement);

    expect(f2c_parse_statement(&unit, "case default", 16U, &statement), "CASE DEFAULT parses");
    expect(statement.kind == F2C_STMT_CASE && statement.case_syntax_valid &&
               statement.case_default && statement.case_range_count == 0U,
           "CASE DEFAULT has a dedicated typed-IR flag without synthetic expressions");
    f2c_statement_free(&statement);

    expect(f2c_parse_statement(&unit, "case (1, 3:5) choice", 16U, &statement),
           "named CASE branch parses");
    expect(statement.kind == F2C_STMT_CASE && statement.case_syntax_valid &&
               statement.construct_syntax_valid && statement.construct_name != NULL &&
               strcmp(statement.construct_name, "choice") == 0 && statement.case_range_count == 2U,
           "CASE branch retains its optional SELECT construct association name");
    f2c_statement_free(&statement);

    expect(f2c_parse_statement(&unit, "case ('a':'z', 'xy')", 16U, &statement),
           "character CASE ranges parse");
    expect(statement.case_range_count == 2U && statement.case_ranges[0].lower != NULL &&
               statement.case_ranges[0].lower->type == TYPE_CHARACTER &&
               statement.case_ranges[0].upper != NULL &&
               statement.case_ranges[0].upper->type == TYPE_CHARACTER,
           "CASE range endpoints retain their expression types");
    f2c_statement_free(&statement);

    expect(f2c_parse_statement(&unit, "where (work > 0.0) work = work + 1.0", 16U, &statement),
           "single-line WHERE parses");
    expect(statement.kind == F2C_STMT_WHERE && !statement.block && statement.expression != NULL &&
               statement.expression->type == TYPE_LOGICAL && statement.expression->rank == 2U &&
               statement.nested != NULL && statement.nested->kind == F2C_STMT_ASSIGNMENT,
           "single-line WHERE owns its array mask and nested assignment AST");
    f2c_statement_free(&statement);

    expect(f2c_parse_statement(&unit, "masking: where (work > 0.0)", 16U, &statement),
           "named WHERE construct parses");
    expect(statement.kind == F2C_STMT_WHERE && statement.block &&
               statement.construct_name != NULL &&
               strcmp(statement.construct_name, "masking") == 0 && statement.expression != NULL &&
               statement.expression->rank == 2U,
           "named WHERE retains its construct identity and typed array mask");
    f2c_statement_free(&statement);

    expect(f2c_parse_statement(&unit, "elsewhere (work < 0.0) masking", 16U, &statement),
           "masked named ELSEWHERE parses");
    expect(statement.kind == F2C_STMT_ELSEWHERE && statement.expression != NULL &&
               statement.expression->type == TYPE_LOGICAL && statement.expression->rank == 2U &&
               statement.construct_name != NULL && strcmp(statement.construct_name, "masking") == 0,
           "masked ELSEWHERE owns its branch mask and WHERE association name");
    f2c_statement_free(&statement);

    expect(f2c_parse_statement(&unit, "elsewhere masking", 16U, &statement),
           "default named ELSEWHERE parses");
    expect(statement.kind == F2C_STMT_ELSEWHERE && statement.expression == NULL &&
               statement.construct_name != NULL && strcmp(statement.construct_name, "masking") == 0,
           "default ELSEWHERE is represented without a synthetic mask expression");
    f2c_statement_free(&statement);

    expect(f2c_parse_statement(&unit, "end where masking", 16U, &statement),
           "named END WHERE parses");
    expect(statement.kind == F2C_STMT_END_WHERE && statement.construct_name != NULL &&
               strcmp(statement.construct_name, "masking") == 0,
           "END WHERE retains its construct association name");
    f2c_statement_free(&statement);

    expect(f2c_parse_statement(&unit, "goto (10, 20, 30), n", 16U, &statement),
           "computed GOTO parses");
    expect(statement.kind == F2C_STMT_GOTO && statement.label_count == 3U &&
               statement.expression != NULL,
           "computed GOTO owns labels and selector AST");
    f2c_statement_free(&statement);

    expect(f2c_parse_statement(&unit, "go to 90", 17U, &statement), "direct GOTO parses");
    expect(statement.kind == F2C_STMT_GOTO && statement.name != NULL &&
               strcmp(statement.name, "90") == 0,
           "direct GOTO owns its target label");
    f2c_statement_free(&statement);

    expect(f2c_parse_statement(&unit, "assign 90 to n", 17U, &statement),
           "assigned-label statement parses");
    expect(statement.kind == F2C_STMT_ASSIGN_LABEL && statement.name != NULL &&
               strcmp(statement.name, "n") == 0 && statement.label_count == 1U &&
               strcmp(statement.labels[0], "90") == 0 && statement.expression != NULL,
           "ASSIGN owns its target variable expression and statement label");
    f2c_statement_free(&statement);

    expect(f2c_parse_statement(&unit, "go to n, (90, 100)", 17U, &statement),
           "assigned GOTO with an allowed-label list parses");
    expect(statement.kind == F2C_STMT_ASSIGNED_GOTO && statement.name != NULL &&
               strcmp(statement.name, "n") == 0 && statement.label_count == 2U &&
               statement.expression != NULL,
           "assigned GOTO owns its selector and allowed labels");
    f2c_statement_free(&statement);

    expect(f2c_parse_statement(&unit, "go to n", 17U, &statement),
           "assigned GOTO without an allowed-label list parses");
    expect(statement.kind == F2C_STMT_ASSIGNED_GOTO && statement.label_count == 0U &&
               statement.expression != NULL,
           "bare assigned GOTO retains its selector for unit-level target resolution");
    f2c_statement_free(&statement);

    expect(f2c_parse_statement(&unit, "call consume(x, n + 1)", 18U, &statement), "CALL parses");
    expect(statement.kind == F2C_STMT_CALL && statement.name != NULL &&
               strcmp(statement.name, "consume") == 0 && statement.item_count == 2U &&
               statement.arguments != NULL && statement.arguments[0] != NULL &&
               statement.arguments[1] != NULL,
           "CALL owns its procedure name, actual texts, and argument ASTs");
    f2c_statement_free(&statement);

    expect(f2c_parse_statement(&unit, "call move_alloc(from=x, to=y)", 18U, &statement),
           "MOVE_ALLOC parses");
    expect(statement.kind == F2C_STMT_MOVE_ALLOC && statement.name != NULL &&
               strcmp(statement.name, "move_alloc") == 0 && statement.item_count == 2U &&
               statement.arguments != NULL && statement.arguments[0] != NULL &&
               statement.arguments[0]->kind == F2C_EXPR_KEYWORD_ARGUMENT,
           "MOVE_ALLOC owns a dedicated typed statement node and argument ASTs");
    f2c_statement_free(&statement);

    expect(f2c_parse_statement(&unit, "20 continue", 19U, &statement), "labeled statement parses");
    expect(statement.kind == F2C_STMT_LABEL && statement.name != NULL &&
               strcmp(statement.name, "20") == 0 && statement.nested != NULL &&
               statement.nested->kind == F2C_STMT_CONTINUE,
           "label owns its normalized target and nested statement IR");
    f2c_statement_free(&statement);

    expect(f2c_parse_statement(&unit, "close(9)", 20U, &statement), "CLOSE parses");
    expect(statement.kind == F2C_STMT_CLOSE && statement.control_count == 1U &&
               statement.io_controls != NULL &&
               statement.io_controls[0].kind == F2C_IO_CONTROL_POSITIONAL &&
               statement.io_controls[0].value != NULL &&
               statement.io_controls[0].value->type == TYPE_INTEGER,
           "CLOSE owns a typed positional unit control AST");
    f2c_statement_free(&statement);

    expect(f2c_parse_statement(&unit, "backspace 9", 20U, &statement), "BACKSPACE parses");
    expect(statement.kind == F2C_STMT_BACKSPACE && statement.io_syntax_valid &&
               statement.control_count == 1U &&
               statement.io_controls[0].kind == F2C_IO_CONTROL_POSITIONAL &&
               statement.io_controls[0].value != NULL &&
               statement.io_controls[0].value->type == TYPE_INTEGER,
           "BACKSPACE shorthand owns a typed positional unit control");
    f2c_statement_free(&statement);

    expect(f2c_parse_statement(&unit, "endfile(unit=9, iostat=n)", 20U, &statement),
           "ENDFILE parses");
    expect(statement.kind == F2C_STMT_ENDFILE && statement.io_syntax_valid &&
               statement.control_count == 2U &&
               statement.io_controls[0].kind == F2C_IO_CONTROL_UNIT &&
               statement.io_controls[1].kind == F2C_IO_CONTROL_IOSTAT &&
               statement.io_controls[1].value != NULL &&
               statement.io_controls[1].value->symbol == &symbols[1],
           "ENDFILE owns canonical keyword controls and bound expressions");
    f2c_statement_free(&statement);

    expect(f2c_parse_statement(&unit, "inquire(unit=9, opened=done, number=n)", 20U,
                               &statement),
           "INQUIRE parses");
    expect(statement.kind == F2C_STMT_INQUIRE && statement.io_syntax_valid &&
               statement.control_count == 3U &&
               statement.io_controls[1].kind == F2C_IO_CONTROL_OPENED &&
               statement.io_controls[1].value != NULL &&
               statement.io_controls[1].value->symbol == &symbols[2] &&
               statement.io_controls[2].kind == F2C_IO_CONTROL_NUMBER,
           "INQUIRE owns typed query-result designators");
    f2c_statement_free(&statement);

    expect(f2c_parse_statement(&unit, "allocate(work(n, 2), stat=n)", 21U, &statement),
           "ALLOCATE parses");
    expect(statement.kind == F2C_STMT_ALLOCATE && statement.item_count == 2U &&
               statement.arguments != NULL &&
               statement.arguments[0]->kind == F2C_EXPR_ARRAY_REFERENCE &&
               statement.arguments[1]->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
               strcmp(statement.arguments[1]->text, "stat") == 0,
           "ALLOCATE owns target bounds and keyword control ASTs");
    f2c_statement_free(&statement);

    expect(f2c_parse_statement(&unit, "allocate(character(len=n + 1) :: values(0:n), stat=n)", 21U,
                               &statement),
           "typed deferred CHARACTER ALLOCATE parses");
    expect(statement.kind == F2C_STMT_ALLOCATE && statement.tail != NULL &&
               strcmp(statement.tail, "character(len=n + 1)") == 0 &&
               statement.allocation_character_length != NULL &&
               statement.allocation_character_length->kind == F2C_EXPR_BINARY &&
               statement.allocation_character_length->type == TYPE_INTEGER &&
               statement.item_count == 2U &&
               statement.arguments[0]->kind == F2C_EXPR_ARRAY_REFERENCE &&
               statement.arguments[0]->children[0]->kind == F2C_EXPR_ARRAY_SECTION,
           "typed ALLOCATE owns its CHARACTER length and explicit bounds as AST nodes");
    f2c_statement_free(&statement);

    expect(f2c_parse_statement(&unit, "read(5, fmt='(A8)', end=90, iostat=n) x", 22U, &statement),
           "READ parses");
    expect(statement.kind == F2C_STMT_READ && statement.control_count == 4U &&
               statement.io_controls != NULL && statement.io_controls[1].keyword != NULL &&
               statement.io_controls[1].kind == F2C_IO_CONTROL_FMT &&
               strcmp(statement.io_controls[1].keyword, "fmt") == 0 &&
               statement.io_controls[1].value != NULL && statement.item_count == 1U &&
               statement.arguments[0] != NULL,
           "READ owns control-list and item ASTs");
    f2c_statement_free(&statement);

    expect(f2c_parse_statement(&unit, "print '(A,1X,F6.2)', 'x,y', (work(n,1), n=1,2)", 23U,
                               &statement),
           "formatted PRINT parses");
    expect(statement.kind == F2C_STMT_PRINT && statement.io_syntax_valid &&
               statement.control_count == 1U && statement.io_controls != NULL &&
               statement.io_controls[0].kind == F2C_IO_CONTROL_FMT &&
               statement.io_controls[0].value != NULL &&
               statement.io_controls[0].value->kind == F2C_EXPR_STRING_LITERAL &&
               statement.item_count == 2U && statement.io_item_count == 2U &&
               statement.arguments != NULL && statement.arguments[0] != NULL &&
               statement.io_items[1].implied_do && statement.io_items[1].child_count == 1U,
           "PRINT owns its format, comma-safe output items, and implied-DO ASTs");
    f2c_statement_free(&statement);

    expect(f2c_parse_statement(&unit, "print *", 23U, &statement), "empty PRINT parses");
    expect(statement.kind == F2C_STMT_PRINT && statement.io_syntax_valid &&
               statement.control_count == 1U && statement.io_controls[0].asterisk &&
               statement.item_count == 0U && statement.io_item_count == 0U,
           "PRINT without an output-item list retains its list-directed format AST");
    f2c_statement_free(&statement);

    expect(f2c_parse_statement(&unit, "print *,", 23U, &statement),
           "malformed PRINT remains representable");
    expect(statement.kind == F2C_STMT_PRINT && !statement.io_syntax_valid,
           "a trailing PRINT comma is retained as an explicit syntax failure");
    f2c_statement_free(&statement);

    expect(f2c_parse_statement(&unit, "write(*,*) (work(n, 1), n=1, 2)", 24U, &statement),
           "I/O implied DO parses");
    expect(statement.kind == F2C_STMT_WRITE && statement.io_item_count == 1U &&
               statement.io_items[0].implied_do && statement.io_items[0].child_count == 1U &&
               statement.io_items[0].children[0].expression != NULL &&
               statement.io_items[0].iterator != NULL && statement.io_items[0].initial != NULL &&
               statement.io_items[0].limit != NULL && statement.io_items[0].step != NULL,
           "I/O implied DO owns nested items and all control expressions");
    f2c_statement_free(&statement);

    expect(f2c_parse_statement(&unit, "data work / 3*1.0, 2.0 /, (work(1,n), n=1,2) / 4.0, 5.0 /",
                               25U, &statement),
           "DATA groups parse");
    expect(statement.kind == F2C_STMT_DATA && statement.data_syntax_valid &&
               statement.data_group_count == 2U &&
               statement.data_groups[0].target_count == 1U &&
               statement.data_groups[0].value_count == 2U &&
               statement.data_groups[0].values[0].repeat != NULL &&
               statement.data_groups[0].values[0].repeat->kind == F2C_EXPR_INTEGER_LITERAL &&
               statement.data_groups[0].values[0].expression != NULL &&
               statement.data_groups[1].target_count == 1U &&
               statement.data_groups[1].targets[0].implied_do &&
               statement.data_groups[1].targets[0].child_count == 1U,
           "DATA owns groups, repetition expressions, values, and implied-DO targets");
    f2c_statement_free(&statement);

    expect(f2c_parse_statement(&unit, "data text / 'a/b,c' /", 26U, &statement),
           "DATA character delimiters parse from tokens");
    expect(statement.kind == F2C_STMT_DATA && statement.data_syntax_valid &&
               statement.data_group_count == 1U && statement.data_groups[0].value_count == 1U &&
               strcmp(statement.data_groups[0].values[0].text, "'a/b,c'") == 0,
           "slashes and commas inside character values never split DATA groups");
    f2c_statement_free(&statement);

    expect(f2c_parse_statement(&unit, "data first / 1 / second / 2 /", 27U, &statement),
           "malformed DATA remains representable for diagnostics");
    expect(statement.kind == F2C_STMT_DATA && !statement.data_syntax_valid,
           "missing DATA group separators are retained as explicit syntax failure");
    f2c_statement_free(&statement);

    if (failures != 0) {
        fprintf(stderr, "%d statement AST test(s) failed\n", failures);
        return 1;
    }
    puts("all statement AST tests passed");
    return 0;
}
