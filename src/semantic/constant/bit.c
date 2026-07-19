#include "semantic/constant/private.h"

#include <limits.h>

static unsigned kind_width(int kind) {
    return kind == 1 || kind == 2 || kind == 4 || kind == 8 ? (unsigned)kind * 8U : 0U;
}

static uint64_t low_mask(unsigned width) {
    return width == 64U ? UINT64_MAX : (UINT64_C(1) << width) - UINT64_C(1);
}

static uint64_t integer_bits(int64_t value, unsigned width) {
    return (uint64_t)value & low_mask(width);
}

static int64_t signed_value(uint64_t bits, unsigned width) {
    const uint64_t mask = low_mask(width);
    const uint64_t sign = UINT64_C(1) << (width - 1U);
    const uint64_t magnitude = ((~bits) & mask) + UINT64_C(1);
    bits &= mask;
    if ((bits & sign) == 0U)
        return (int64_t)bits;
    if (width == 64U && magnitude == (UINT64_C(1) << 63U))
        return INT64_MIN;
    return -(int64_t)magnitude;
}

static int valid_position(int64_t position, unsigned width) {
    return position >= 0 && (uint64_t)position < (uint64_t)width;
}

static int fold_shift(uint64_t bits, unsigned width, int64_t shift, uint64_t *result) {
    const uint64_t magnitude = shift < 0 ? (uint64_t)(-(shift + 1)) + UINT64_C(1) : (uint64_t)shift;
    if (magnitude > width)
        return 0;
    if (magnitude == width) {
        *result = UINT64_C(0);
    } else if (shift < 0) {
        *result = bits >> (unsigned)magnitude;
    } else {
        *result = (bits << (unsigned)magnitude) & low_mask(width);
    }
    return 1;
}

static int fold_circular_shift(uint64_t bits, unsigned width, int64_t shift, int64_t size,
                               uint64_t *result) {
    uint64_t field_mask;
    uint64_t field;
    int64_t normalized;
    if (size <= 0 || (uint64_t)size > width || shift < -size || shift > size)
        return 0;
    field_mask = low_mask((unsigned)size);
    field = bits & field_mask;
    normalized = shift % size;
    if (normalized < 0)
        normalized += size;
    if (normalized != 0) {
        const unsigned amount = (unsigned)normalized;
        field = ((field << amount) | (field >> ((unsigned)size - amount))) & field_mask;
    }
    *result = (bits & ~field_mask) | field;
    return 1;
}

int f2c_constant_fold_bit_intrinsic(F2cIntrinsicId intrinsic, int integer_kind,
                                    const int64_t *arguments, size_t argument_count,
                                    int64_t *result) {
    const unsigned width = kind_width(integer_kind);
    uint64_t bits;
    uint64_t folded;
    int64_t position;
    int64_t length;
    if (result == NULL || width == 0U)
        return 0;
    if (intrinsic == F2C_INTRINSIC_BIT_SIZE) {
        *result = (int64_t)width;
        return 1;
    }
    if (arguments == NULL || argument_count == 0U)
        return 0;
    bits = integer_bits(arguments[0], width);
    switch (intrinsic) {
    case F2C_INTRINSIC_BTEST:
        if (argument_count != 2U || !valid_position(arguments[1], width))
            return 0;
        *result = (bits & (UINT64_C(1) << (unsigned)arguments[1])) != 0U;
        return 1;
    case F2C_INTRINSIC_IAND:
    case F2C_INTRINSIC_IEOR:
    case F2C_INTRINSIC_IOR:
        if (argument_count != 2U)
            return 0;
        folded = integer_bits(arguments[1], width);
        if (intrinsic == F2C_INTRINSIC_IAND)
            folded = bits & folded;
        else if (intrinsic == F2C_INTRINSIC_IEOR)
            folded = bits ^ folded;
        else
            folded = bits | folded;
        break;
    case F2C_INTRINSIC_IBCLR:
    case F2C_INTRINSIC_IBSET:
        if (argument_count != 2U || !valid_position(arguments[1], width))
            return 0;
        folded = UINT64_C(1) << (unsigned)arguments[1];
        folded = intrinsic == F2C_INTRINSIC_IBCLR ? bits & ~folded : bits | folded;
        break;
    case F2C_INTRINSIC_IBITS:
        if (argument_count != 3U)
            return 0;
        position = arguments[1];
        length = arguments[2];
        if (position < 0 || length < 0 || (uint64_t)position > width ||
            (uint64_t)length > width - (uint64_t)position)
            return 0;
        folded =
            length == 0 ? UINT64_C(0) : (bits >> (unsigned)position) & low_mask((unsigned)length);
        break;
    case F2C_INTRINSIC_ISHFT:
        if (argument_count != 2U || !fold_shift(bits, width, arguments[1], &folded))
            return 0;
        break;
    case F2C_INTRINSIC_ISHFTC:
        if (argument_count < 2U || argument_count > 3U ||
            !fold_circular_shift(bits, width, arguments[1],
                                 argument_count == 3U ? arguments[2] : (int64_t)width, &folded))
            return 0;
        break;
    case F2C_INTRINSIC_NOT:
        if (argument_count != 1U)
            return 0;
        folded = (~bits) & low_mask(width);
        break;
    case F2C_INTRINSIC_NONE:
    case F2C_INTRINSIC_BIT_SIZE:
    case F2C_INTRINSIC_MVBITS:
    default:
        return 0;
    }
    *result = signed_value(folded, width);
    return 1;
}
