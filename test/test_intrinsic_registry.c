#include "semantic/intrinsic.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

static int permitted_untyped_extension(const char *name) {
    static const char *const extensions[] = {
        "abs1", "abssq", "cabs1", "cabs2", "omp_get_num_threads", "omp_get_thread_num",
    };
    size_t index;
    for (index = 0U; index < sizeof(extensions) / sizeof(extensions[0]); ++index)
        if (strcmp(name, extensions[index]) == 0)
            return 1;
    return 0;
}

static void test_identity_completeness(void) {
    F2cIntrinsicId intrinsic;
    size_t descriptor_count = 0U;
    for (intrinsic = (F2cIntrinsicId)(F2C_INTRINSIC_NONE + 1);
         intrinsic < F2C_INTRINSIC_ID_COUNT; intrinsic = (F2cIntrinsicId)(intrinsic + 1)) {
        const F2cIntrinsicDescriptor *descriptor = f2c_intrinsic_descriptor(intrinsic);
        const F2cIntrinsicArgumentSchema *schema = f2c_intrinsic_argument_schema(intrinsic);
        F2cIntrinsicId other;
        size_t argument;
        assert(descriptor != NULL);
        assert(schema != NULL);
        assert(descriptor->id == intrinsic);
        assert(descriptor->canonical_name != NULL);
        assert(descriptor->canonical_name[0] != '\0');
        assert(f2c_find_intrinsic_descriptor(descriptor->canonical_name) == descriptor);
        assert(schema->count >= 1U);
        assert(schema->count <= F2C_INTRINSIC_ARGUMENT_LIMIT);
        assert((schema->required_mask >> schema->count) == 0U);
        for (argument = 0U; argument < schema->count; ++argument) {
            size_t other_argument;
            assert(schema->names[argument] != NULL);
            assert(schema->names[argument][0] != '\0');
            for (other_argument = argument + 1U; other_argument < schema->count; ++other_argument)
                assert(strcmp(schema->names[argument], schema->names[other_argument]) != 0);
        }
        for (other = (F2cIntrinsicId)(intrinsic + 1); other < F2C_INTRINSIC_ID_COUNT;
             other = (F2cIntrinsicId)(other + 1)) {
            const F2cIntrinsicDescriptor *candidate = f2c_intrinsic_descriptor(other);
            assert(candidate != NULL);
            assert(strcmp(descriptor->canonical_name, candidate->canonical_name) != 0);
        }
        if (descriptor->procedure_kind == F2C_INTRINSIC_PROCEDURE_FUNCTION) {
            const F2cIntrinsicSignature *signature =
                f2c_find_intrinsic(descriptor->canonical_name);
            assert(signature != NULL);
            assert(signature->id == intrinsic);
        } else {
            assert(f2c_is_intrinsic_subroutine(descriptor->canonical_name));
        }
        ++descriptor_count;
    }
    assert(descriptor_count == (size_t)F2C_INTRINSIC_ID_COUNT - 1U);
    assert(f2c_intrinsic_descriptor(F2C_INTRINSIC_NONE) == NULL);
    assert(f2c_intrinsic_descriptor(F2C_INTRINSIC_ID_COUNT) == NULL);
    assert(f2c_find_intrinsic_descriptor("not_an_intrinsic") == NULL);
}

static void test_signature_identity(void) {
    size_t index;
    size_t untyped = 0U;
    for (index = 0U; index < f2c_intrinsic_signature_count(); ++index) {
        const F2cIntrinsicSignature *signature = f2c_intrinsic_signature_at(index);
        assert(signature != NULL);
        assert(signature->name != NULL);
        assert(signature->minimum_arguments <= signature->maximum_arguments);
        assert(f2c_find_intrinsic(signature->name) == signature);
        if (signature->id == F2C_INTRINSIC_NONE) {
            assert(permitted_untyped_extension(signature->name));
            ++untyped;
        } else {
            const F2cIntrinsicArgumentSchema *schema =
                f2c_intrinsic_argument_schema(signature->id);
            assert(f2c_intrinsic_descriptor(signature->id) != NULL);
            assert(schema != NULL);
            assert(schema->variadic || signature->maximum_arguments <= schema->count);
        }
    }
    assert(untyped == 6U);
    assert(f2c_intrinsic_signature_at(f2c_intrinsic_signature_count()) == NULL);
}

static void test_standard_and_family_metadata(void) {
    const F2cIntrinsicDescriptor *allocated =
        f2c_intrinsic_descriptor(F2C_INTRINSIC_ALLOCATED);
    const F2cIntrinsicDescriptor *findloc = f2c_intrinsic_descriptor(F2C_INTRINSIC_FINDLOC);
    const F2cIntrinsicDescriptor *isnan = f2c_intrinsic_descriptor(F2C_INTRINSIC_ISNAN);
    const F2cIntrinsicDescriptor *cpu_time = f2c_intrinsic_descriptor(F2C_INTRINSIC_CPU_TIME);
    assert(allocated->standard == F2C_INTRINSIC_STANDARD_FORTRAN_90);
    assert(findloc->standard == F2C_INTRINSIC_STANDARD_FORTRAN_2008);
    assert(isnan->standard == F2C_INTRINSIC_STANDARD_EXTENSION);
    assert(cpu_time->standard == F2C_INTRINSIC_STANDARD_FORTRAN_95);
    assert(cpu_time->procedure_kind == F2C_INTRINSIC_PROCEDURE_SUBROUTINE);
    assert(f2c_intrinsic_is_bit(F2C_INTRINSIC_ISHFTC));
    assert(f2c_intrinsic_is_character(F2C_INTRINSIC_VERIFY));
    assert(f2c_intrinsic_is_conversion(F2C_INTRINSIC_CMPLX));
    assert(f2c_intrinsic_is_mathematical(F2C_INTRINSIC_SQRT));
    assert(f2c_intrinsic_is_numeric_model(F2C_INTRINSIC_EPSILON));
    assert(f2c_intrinsic_is_numeric_operation(F2C_INTRINSIC_MODULO));
    assert(f2c_intrinsic_is_real_representation(F2C_INTRINSIC_NEAREST));
    assert(f2c_intrinsic_is_reduction(F2C_INTRINSIC_SUM));
    assert(f2c_intrinsic_is_transformational(F2C_INTRINSIC_RESHAPE));
    assert(f2c_intrinsic_has_family(F2C_INTRINSIC_LBOUND,
                                    F2C_INTRINSIC_FAMILY_ARRAY_INQUIRY));
    assert(f2c_intrinsic_has_family(F2C_INTRINSIC_SIZE,
                                    F2C_INTRINSIC_FAMILY_ASSUMED_SIZE_INQUIRY));
    assert(!f2c_intrinsic_is_mathematical(F2C_INTRINSIC_SIZE));
}

int main(void) {
    test_identity_completeness();
    test_signature_identity();
    test_standard_and_family_metadata();
    return 0;
}
