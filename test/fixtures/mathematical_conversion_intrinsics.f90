module mathematical_conversion_constants
  implicit none

  complex(kind=8), parameter :: constant_base = cmplx(0.25_8, -0.5_8, kind=8)
  complex(kind=8), parameter :: constant_acos = acos(constant_base)
  complex(kind=8), parameter :: constant_asin = asin(constant_base)
  complex(kind=8), parameter :: constant_atan = atan(constant_base)
  complex(kind=8), parameter :: constant_cos = cos(constant_base)
  complex(kind=8), parameter :: constant_cosh = cosh(constant_base)
  complex(kind=8), parameter :: constant_exp = exp(constant_base)
  complex(kind=8), parameter :: constant_log = log(constant_base)
  complex(kind=8), parameter :: constant_sin = sin(constant_base)
  complex(kind=8), parameter :: constant_sinh = sinh(constant_base)
  complex(kind=8), parameter :: constant_sqrt = sqrt(constant_base)
  complex(kind=8), parameter :: constant_tan = tan(constant_base)
  complex(kind=8), parameter :: constant_tanh = tanh(constant_base)
  complex(kind=8), parameter :: constant_conjg = conjg(constant_base)
  complex(kind=8), parameter :: constant_arithmetic = &
    (constant_base + constant_conjg) * cmplx(2.0_8, 1.0_8, kind=8) / &
    cmplx(1.0_8, -1.0_8, kind=8)
  complex(kind=8), parameter :: constant_power = constant_base**3
  real(kind=8), parameter :: constant_real = real(constant_arithmetic, kind=8)
  real(kind=8), parameter :: constant_imaginary = aimag(constant_arithmetic)
  real(kind=8), parameter :: constant_magnitude = abs(constant_arithmetic)
  integer(kind=2), parameter :: constant_integer = int(constant_arithmetic, kind=2)
end module mathematical_conversion_constants

program mathematical_conversion_intrinsics
  use, intrinsic :: iso_fortran_env, only: working_kind => real32, wide_kind => real64, &
    tiny_kind => int8, short_kind => int16, default_integer_kind => int32, &
    long_integer_kind => int64, character_storage_size, error_unit, file_storage_size, &
    input_unit, iostat_end, iostat_eor, numeric_storage_size, output_unit
  use mathematical_conversion_constants
  implicit none

  integer(kind=tiny_kind) :: i1
  integer(kind=short_kind) :: i2
  integer(kind=default_integer_kind) :: i4
  integer(kind=long_integer_kind) :: i8
  integer(kind=long_integer_kind) :: converted(4)
  logical :: flags(4)
  logical(kind=1) :: compact_flags(4)
  logical(kind=8) :: wide_flag
  real :: r4, values(4)
  double precision :: r8, wide(4)
  complex(kind=working_kind) :: c4
  complex(kind=wide_kind) :: c8
  integer, parameter :: folded_integer = int(3.75) + max(a2=4, a1=2)
  real, parameter :: folded_real = sqrt(4.0) + real(2)
  integer, parameter :: folded_legacy_integer = max1(3.75, -2.25)
  real, parameter :: folded_legacy_real = amax0(7, -3)
  logical(kind=1), parameter :: folded_logical = logical(l=.true., kind=1)
  integer, parameter :: environment_total = character_storage_size + error_unit + &
    file_storage_size + input_unit + iostat_end + iostat_eor + numeric_storage_size + output_unit

  if (folded_integer /= 7 .or. folded_real /= 4.0 .or. &
      folded_legacy_integer /= 3 .or. folded_legacy_real /= 7.0 .or. &
      .not. folded_logical .or. environment_total /= 56) error stop 1

  i1 = int(-12.75, kind=tiny_kind)
  i2 = int(a=300.75d0, kind=short_kind)
  i4 = int(cmplx(-42.5, 99.0))
  i8 = int(kind=long_integer_kind, a=cmplx(5000000000.75d0, -3.0d0, kind=wide_kind))
  if (i1 /= -12_tiny_kind .or. i2 /= 300_short_kind .or. &
      i4 /= -42_default_integer_kind .or. i8 /= 5000000000_long_integer_kind) error stop 2

  r4 = real(i8)
  r8 = real(a=cmplx(1.25, -9.0), kind=8) + dble(cmplx(2.5d0, 8.0d0, kind=8))
  if (abs(r4 - 5.0e9) > 1024.0 .or. abs(r8 - 3.75d0) > epsilon(r8)) error stop 3

  c4 = cmplx(kind=working_kind, y=-2.5, x=1.25)
  c8 = cmplx(3.5d0, -4.5d0, kind=wide_kind)
  if (abs(real(c4) - 1.25) > epsilon(r4) .or. &
      abs(aimag(c4) + 2.5) > epsilon(r4)) error stop 4
  if (abs(real(c8) - 3.5d0) > epsilon(r8) .or. &
      abs(aimag(c8) + 4.5d0) > epsilon(r8)) error stop 5
  if (abs(conjg(c4) - cmplx(1.25, 2.5)) > 8.0 * epsilon(r4)) error stop 6

  r4 = 0.25
  if (abs(acos(r4) - 1.3181161) > 8.0 * epsilon(r4)) error stop 7
  if (abs(asin(r4) - 0.25268027) > 8.0 * epsilon(r4)) error stop 8
  if (abs(atan(r4) - 0.24497867) > 8.0 * epsilon(r4)) error stop 9
  if (abs(atan2(y=r4, x=1.0) - 0.24497867) > 8.0 * epsilon(r4)) error stop 10
  if (abs(cos(r4) - 0.9689124) > 8.0 * epsilon(r4)) error stop 11
  if (abs(cosh(r4) - 1.0314131) > 8.0 * epsilon(r4)) error stop 12
  if (abs(exp(r4) - 1.2840254) > 8.0 * epsilon(r4)) error stop 13
  if (abs(log(exp(r4)) - r4) > 8.0 * epsilon(r4)) error stop 14
  if (abs(log10(100.0) - 2.0) > epsilon(r4)) error stop 15
  if (abs(sin(r4) - 0.24740396) > 8.0 * epsilon(r4)) error stop 16
  if (abs(sinh(r4) - 0.25261232) > 8.0 * epsilon(r4)) error stop 17
  if (abs(sqrt(r4) - 0.5) > epsilon(r4)) error stop 18
  if (abs(tan(r4) - 0.25534192) > 8.0 * epsilon(r4)) error stop 19
  if (abs(tanh(r4) - 0.24491866) > 8.0 * epsilon(r4)) error stop 20
  if (abs(dprod(r4, 4.0) - 1.0d0) > epsilon(r8)) error stop 29

  c4 = cmplx(0.25, -0.5)
  if (abs(cos(c4) - ccos_reference(c4)) > 32.0 * epsilon(r4)) error stop 21
  if (abs(exp(c4) - cexp_reference(c4)) > 32.0 * epsilon(r4)) error stop 22
  if (abs(sin(c4) - csin_reference(c4)) > 32.0 * epsilon(r4)) error stop 23
  if (abs(sqrt(c4) * sqrt(c4) - c4) > 32.0 * epsilon(r4)) error stop 24
  if (abs(exp(log(c4)) - c4) > 64.0 * epsilon(r4)) error stop 25
  if (abs(cos(acos(c4)) - c4) > 64.0 * epsilon(r4)) error stop 30
  if (abs(sin(asin(c4)) - c4) > 64.0 * epsilon(r4)) error stop 31
  if (abs(tan(atan(c4)) - c4) > 64.0 * epsilon(r4)) error stop 32
  if (abs(tan(c4) - sin(c4) / cos(c4)) > 64.0 * epsilon(r4)) error stop 33
  if (abs(tanh(c4) - sinh(c4) / cosh(c4)) > 64.0 * epsilon(r4)) error stop 34
  if (abs(constant_acos - acos(constant_base)) > 128.0d0 * epsilon(r8)) error stop 35
  if (abs(constant_asin - asin(constant_base)) > 128.0d0 * epsilon(r8)) error stop 36
  if (abs(constant_atan - atan(constant_base)) > 128.0d0 * epsilon(r8)) error stop 37
  if (abs(constant_cos - cos(constant_base)) > 128.0d0 * epsilon(r8)) error stop 38
  if (abs(constant_cosh - cosh(constant_base)) > 128.0d0 * epsilon(r8)) error stop 39
  if (abs(constant_exp - exp(constant_base)) > 128.0d0 * epsilon(r8)) error stop 40
  if (abs(constant_log - log(constant_base)) > 128.0d0 * epsilon(r8)) error stop 41
  if (abs(constant_sin - sin(constant_base)) > 128.0d0 * epsilon(r8)) error stop 42
  if (abs(constant_sinh - sinh(constant_base)) > 128.0d0 * epsilon(r8)) error stop 43
  if (abs(constant_sqrt - sqrt(constant_base)) > 128.0d0 * epsilon(r8)) error stop 44
  if (abs(constant_tan - tan(constant_base)) > 128.0d0 * epsilon(r8)) error stop 45
  if (abs(constant_tanh - tanh(constant_base)) > 128.0d0 * epsilon(r8)) error stop 46
  if (constant_conjg /= conjg(constant_base)) error stop 47
  if (abs(constant_arithmetic - cmplx(0.25d0, 0.75d0, kind=8)) > &
      128.0d0 * epsilon(r8)) error stop 48
  if (abs(constant_power - cmplx(-0.171875d0, 0.03125d0, kind=8)) > &
      128.0d0 * epsilon(r8)) error stop 49
  if (constant_real /= 0.25d0 .or. constant_imaginary /= 0.75d0 .or. &
      abs(constant_magnitude - sqrt(0.625d0)) > epsilon(r8) .or. &
      constant_integer /= 0_2) error stop 50

  i1 = max(-120_1, 100_1)
  i2 = min(30000_2, -20000_2)
  i4 = max(a2=9, a1=7)
  i8 = min(5000000000_8, 6000000000_8)
  if (i1 /= 100_1 .or. i2 /= -20000_2 .or. i4 /= 9 .or. i8 /= 5000000000_8) error stop 26
  i4 = max1(-3.75, 2.25)
  r4 = amax0(-3, 7) + amin1(4.5, -2.5)
  r8 = dmax1(-3.0d0, 7.0d0) + dmin1(4.5d0, -2.5d0)
  if (i4 /= 2 .or. r4 /= 4.5 .or. r8 /= 4.5d0) error stop 51

  flags = [.true., .false., .true., .false.]
  compact_flags = logical(flags, kind=1)
  wide_flag = logical(l=.true., kind=8)
  if ((compact_flags(1) .neqv. flags(1)) .or. &
      (compact_flags(2) .neqv. flags(2)) .or. &
      (compact_flags(3) .neqv. flags(3)) .or. &
      (compact_flags(4) .neqv. flags(4)) .or. .not. wide_flag) error stop 52

  values = [-4.0, -1.0, 0.25, 9.0]
  values = sin(values) + sqrt(abs(values))
  if (abs(values(1) - (sin(-4.0) + 2.0)) > 8.0 * epsilon(r4) .or. &
      abs(values(4) - (sin(9.0) + 3.0)) > 8.0 * epsilon(r4)) error stop 27
  wide = [-3.75d0, 0.0d0, 4.99d0, 5000000000.25d0]
  converted = int(wide, kind=8)
  if (any(converted /= [-3_8, 0_8, 4_8, 5000000000_8])) error stop 28
  values = [-4.0, -1.0, 0.25, 9.0]
  values = amax1(values, -2.0)
  if (values(1) /= -2.0 .or. values(4) <= -2.0) error stop 53

  print '(A)', 'MATHEMATICAL-CONVERSION-PASS'

contains

  complex function ccos_reference(z)
    complex, intent(in) :: z
    ccos_reference = cmplx(cos(real(z)) * cosh(aimag(z)), &
      -sin(real(z)) * sinh(aimag(z)))
  end function ccos_reference

  complex function cexp_reference(z)
    complex, intent(in) :: z
    cexp_reference = exp(real(z)) * cmplx(cos(aimag(z)), sin(aimag(z)))
  end function cexp_reference

  complex function csin_reference(z)
    complex, intent(in) :: z
    csin_reference = cmplx(sin(real(z)) * cosh(aimag(z)), &
      cos(real(z)) * sinh(aimag(z)))
  end function csin_reference
end program mathematical_conversion_intrinsics
