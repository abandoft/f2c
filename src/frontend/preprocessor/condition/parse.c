#include "frontend/preprocessor/condition/private.h"
#include <stdlib.h>

typedef enum FrameState {
    FRAME_OPERAND,
    FRAME_OPERATOR,
    FRAME_UNARY,
    FRAME_PARENTHESIS,
    FRAME_BINARY,
    FRAME_TRUE_BRANCH,
    FRAME_FALSE_BRANCH
} FrameState;
typedef struct ConditionFrame {
    FrameState state;
    unsigned int minimum_precedence;
    size_t depth;
    int evaluate;
    int selected;
    ConditionOperator operation;
    const char *operation_at;
    IntegerValue left;
    IntegerValue when_true;
} ConditionFrame;
typedef struct ConditionStack {
    ConditionFrame inline_frames[16];
    ConditionFrame *frames;
    ConditionFrame *heap;
    size_t count;
    size_t capacity;
} ConditionStack;
typedef struct OperatorToken {
    const char *text;
    ConditionOperator operation;
    unsigned int precedence;
} OperatorToken;

static const OperatorToken *peek_operator(ExpressionParser *parser) {
    static const OperatorToken tokens[] = {
        {"||", CONDITION_LOGICAL_OR, 2U},    {"&&", CONDITION_LOGICAL_AND, 3U},
        {"|", CONDITION_BIT_OR, 4U},         {"^", CONDITION_BIT_XOR, 5U},
        {"&", CONDITION_BIT_AND, 6U},        {"==", CONDITION_EQUAL, 7U},
        {"!=", CONDITION_NOT_EQUAL, 7U},     {"<=", CONDITION_LESS_EQUAL, 8U},
        {">=", CONDITION_GREATER_EQUAL, 8U}, {"<<", CONDITION_LEFT_SHIFT, 9U},
        {">>", CONDITION_RIGHT_SHIFT, 9U},   {"<", CONDITION_LESS, 8U},
        {">", CONDITION_GREATER, 8U},        {"+", CONDITION_ADD, 10U},
        {"-", CONDITION_SUBTRACT, 10U},      {"*", CONDITION_MULTIPLY, 11U},
        {"/", CONDITION_DIVIDE, 11U},        {"%", CONDITION_REMAINDER, 11U}};
    expression_space(parser);
    for (size_t index = 0U; index < sizeof(tokens) / sizeof(tokens[0]); ++index)
        if (strncmp(parser->cursor, tokens[index].text, strlen(tokens[index].text)) == 0)
            return &tokens[index];
    return NULL;
}
static int push_frame(ExpressionParser *parser, ConditionStack *stack, unsigned int precedence,
                      int evaluate, size_t depth) {
    if (stack->count == stack->capacity) {
        if (stack->capacity > SIZE_MAX / 2U / sizeof(*stack->frames)) {
            f2c_condition_error_code(parser, F2C_DIAGNOSTIC_RESOURCE_LIMIT, parser->cursor,
                                     "preprocessor condition stack capacity exceeded");
            return 0;
        }
        const size_t capacity = stack->capacity * 2U;
        ConditionFrame *replacement =
            (ConditionFrame *)realloc(stack->heap, capacity * sizeof(*stack->frames));
        if (replacement == NULL) {
            f2c_condition_error_code(parser, F2C_DIAGNOSTIC_OUT_OF_MEMORY, parser->cursor,
                                     "out of memory while parsing a preprocessor condition");
            return 0;
        }
        if (stack->heap == NULL)
            memcpy(replacement, stack->inline_frames, sizeof(stack->inline_frames));
        stack->heap = replacement;
        stack->frames = replacement;
        stack->capacity = capacity;
    }
    stack->frames[stack->count++] = (ConditionFrame){.state = FRAME_OPERAND,
                                                     .minimum_precedence = precedence,
                                                     .depth = depth,
                                                     .evaluate = evaluate};
    return 1;
}
static int push_nested(ExpressionParser *parser, ConditionStack *stack, unsigned int precedence,
                       int evaluate, size_t depth) {
    if (depth >= parser->preprocessor->context->limits.max_parse_depth) {
        f2c_condition_error_code(parser, F2C_DIAGNOSTIC_RESOURCE_LIMIT, parser->cursor,
                                 "preprocessor condition expression depth limit exceeded");
        return 0;
    }
    return push_frame(parser, stack, precedence, evaluate, depth + 1U);
}
/* Explicit activation records bound stack usage independently of nesting.
 * Parentheses, unary operands and conditional branches consume language depth;
 * flat binary chains do not consume the native thread stack. */
static IntegerValue parse_condition(ExpressionParser *parser, int evaluate) {
    ConditionStack stack = {0};
    stack.frames = stack.inline_frames;
    stack.capacity = sizeof(stack.inline_frames) / sizeof(stack.inline_frames[0]);
    IntegerValue value = signed_integer(0);
    if (!push_frame(parser, &stack, 1U, evaluate, 0U))
        return value;
    while (stack.count != 0U && !parser->failed) {
        ConditionFrame *frame = &stack.frames[stack.count - 1U];
        expression_space(parser);
        if (frame->state == FRAME_OPERAND) {
            const char token = *parser->cursor;
            if (token == '(') {
                ++parser->cursor;
                frame->state = FRAME_PARENTHESIS;
                if (!push_nested(parser, &stack, 1U, frame->evaluate, frame->depth))
                    break;
                continue;
            }
            if (token == '+' || token == '-' || token == '!' || token == '~') {
                frame->operation = token == '+'   ? CONDITION_ADD
                                   : token == '-' ? CONDITION_SUBTRACT
                                   : token == '!' ? CONDITION_LOGICAL_NOT
                                                  : CONDITION_BIT_NOT;
                frame->operation_at = parser->cursor++;
                frame->state = FRAME_UNARY;
                if (!push_nested(parser, &stack, 12U, frame->evaluate, frame->depth))
                    break;
                continue;
            }
            frame->left = f2c_condition_atom(parser, frame->evaluate);
            frame->state = FRAME_OPERATOR;
            continue;
        }
        if (frame->state == FRAME_OPERATOR) {
            if (*parser->cursor == '?' && frame->minimum_precedence <= 1U) {
                ++parser->cursor;
                frame->selected = integer_true(frame->left);
                frame->state = FRAME_TRUE_BRANCH;
                if (!push_nested(parser, &stack, 1U, frame->evaluate && frame->selected,
                                 frame->depth))
                    break;
                continue;
            }
            const OperatorToken *token = peek_operator(parser);
            if (token != NULL && token->precedence >= frame->minimum_precedence) {
                frame->operation = token->operation;
                frame->operation_at = parser->cursor;
                parser->cursor += strlen(token->text);
                frame->state = FRAME_BINARY;
                int rhs_evaluate = frame->evaluate;
                if (frame->operation == CONDITION_LOGICAL_AND)
                    rhs_evaluate = rhs_evaluate && integer_true(frame->left);
                if (frame->operation == CONDITION_LOGICAL_OR)
                    rhs_evaluate = rhs_evaluate && !integer_true(frame->left);
                if (!push_frame(parser, &stack, token->precedence + 1U, rhs_evaluate, frame->depth))
                    break;
                continue;
            }
            value = frame->left;
            --stack.count;
            if (stack.count == 0U)
                break;
            frame = &stack.frames[stack.count - 1U];
            switch (frame->state) {
            case FRAME_UNARY:
                frame->left = f2c_condition_unary(parser, frame->operation, value, frame->evaluate,
                                                  frame->operation_at);
                frame->state = FRAME_OPERATOR;
                break;
            case FRAME_PARENTHESIS:
                if (!expression_consume(parser, ")"))
                    expression_error(parser, "expected ')' in preprocessor condition");
                frame->left = value;
                frame->state = FRAME_OPERATOR;
                break;
            case FRAME_BINARY:
                frame->left = f2c_condition_binary(parser, frame->operation, frame->left, value,
                                                   frame->evaluate, frame->operation_at);
                frame->state = FRAME_OPERATOR;
                break;
            case FRAME_TRUE_BRANCH:
                if (!expression_consume(parser, ":")) {
                    expression_error(parser, "expected ':' in preprocessor conditional expression");
                    break;
                }
                frame->when_true = value;
                frame->state = FRAME_FALSE_BRANCH;
                if (!push_nested(parser, &stack, 1U, frame->evaluate && !frame->selected,
                                 frame->depth))
                    break;
                break;
            case FRAME_FALSE_BRANCH:
                frame->left = frame->selected ? frame->when_true : value;
                if (frame->when_true.is_unsigned || value.is_unsigned)
                    frame->left = unsigned_integer(integer_unsigned(frame->left));
                if (!frame->evaluate)
                    frame->left =
                        frame->left.is_unsigned ? unsigned_integer(0U) : signed_integer(0);
                frame->state = FRAME_OPERATOR;
                break;
            default:
                expression_error(parser, "invalid preprocessor condition parser state");
                break;
            }
            continue;
        }
        expression_error(parser, "invalid preprocessor condition parser state");
    }
    free(stack.heap);
    return value;
}
int f2c_preprocessor_evaluate_condition(Preprocessor *preprocessor, const char *text, size_t line,
                                        size_t column, int evaluate, int *condition) {
    Buffer expanded = {0};
    expanded.limit = preprocessor->context->limits.max_preprocessed_bytes;
    if (!f2c_preprocessor_expand_condition(preprocessor, text, strlen(text), line, column,
                                           &expanded)) {
        free(expanded.data);
        return 0;
    }
    ExpressionParser parser = {0};
    parser.preprocessor = preprocessor;
    parser.text = expanded.data != NULL ? expanded.data : "";
    parser.cursor = parser.text;
    parser.line = line;
    parser.column = column;
    const IntegerValue value = parse_condition(&parser, evaluate);
    expression_space(&parser);
    if (!parser.failed && *parser.cursor != '\0')
        expression_error(&parser, "unexpected token in preprocessor condition");
    free(expanded.data);
    if (parser.failed)
        return 0;
    *condition = evaluate && integer_true(value);
    return 1;
}
