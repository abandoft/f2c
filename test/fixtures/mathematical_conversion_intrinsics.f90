program mathematical_conversion_intrinsics
  use, intrinsic :: iso_fortran_env, only: real32, real64
  implicit none

  integer, parameter :: working_kind = real32
  integer, parameter :: wide_kind = real64
  integer(kind=1) :: i1
  integer(kind=2) :: i2
  integer(kind=4) :: i4
  integer(kind=8) :: i8
  integer(kind=8) :: converted(4)
  real :: r4, values(4)
  double precision :: r8, wide(4)
  complex(kind=working_kind) :: c4
  complex(kind=wide_kind) :: c8
  integer, parameter :: folded_integer = int(3.75) + max(a2=4, a1=2)
  real, parameter :: folded_real = sqrt(4.0) + real(2)

  if (folded_integer /= 7 .or. folded_real /= 4.0) error stop 1

  i1 = int(-12.75, kind=1)
  i2 = int(a=300.75d0, kind=2)
  i4 = int(cmplx(-42.5, 99.0))
  i8 = int(kind=8, a=cmplx(5000000000.75d0, -3.0d0, kind=8))
  if (i1 /= -12_1 .or. i2 /= 300_2 .or. i4 /= -42 .or. i8 /= 5000000000_8) error stop 2

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

  i1 = max(-120_1, 100_1)
  i2 = min(30000_2, -20000_2)
  i4 = max(a2=9, a1=7)
  i8 = min(5000000000_8, 6000000000_8)
  if (i1 /= 100_1 .or. i2 /= -20000_2 .or. i4 /= 9 .or. i8 /= 5000000000_8) error stop 26

  values = [-4.0, -1.0, 0.25, 9.0]
  values = sin(values) + sqrt(abs(values))
  if (abs(values(1) - (sin(-4.0) + 2.0)) > 8.0 * epsilon(r4) .or. &
      abs(values(4) - (sin(9.0) + 3.0)) > 8.0 * epsilon(r4)) error stop 27
  wide = [-3.75d0, 0.0d0, 4.99d0, 5000000000.25d0]
  converted = int(wide, kind=8)
  if (any(converted /= [-3_8, 0_8, 4_8, 5000000000_8])) error stop 28

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
