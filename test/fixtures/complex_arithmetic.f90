module la_constants
  integer, parameter :: sp = kind(1.0)
end module la_constants

program complex_arithmetic
  implicit none
  use la_constants, only: zone
  complex :: a, b, c
  double complex :: z, q
  double precision :: x
  integer :: status
  external validate_complex

  a = cmplx(1.0, 2.0)
  b = cmplx(3.0, -4.0)
  c = a + b
  if (abs(c - cmplx(4.0, -2.0)) > 1.0e-5) error stop 1

  c = a * b
  if (abs(c - cmplx(11.0, 2.0)) > 1.0e-5) error stop 2

  c = b / a
  if (abs(c - cmplx(-1.0, -2.0)) > 1.0e-5) error stop 3

  c = a / cmplx(0.0, 0.0)
  if (abs(c) <= huge(1.0)) error stop 4

  c = -a
  if (c /= cmplx(-1.0, -2.0)) error stop 5

  c = a ** 2
  if (abs(c - cmplx(-3.0, 4.0)) > 1.0e-5) error stop 6

  status = 3
  c = a ** status
  if (abs(c - cmplx(-11.0, -2.0)) > 1.0e-5) error stop 7

  z = a
  a = cmplx(z)
  if (a /= cmplx(1.0, 2.0)) error stop 8

  if (abs(zone - dcmplx(1.0, 0.0)) > 1.0d-12) error stop 9

  x = huge(1.0d0)
  z = dcmplx(x, 0.0d0)
  if (z / z /= dcmplx(1.0d0, 0.0d0)) error stop 10
  z = dcmplx(0.0d0, x)
  if (z / z /= dcmplx(1.0d0, 0.0d0)) error stop 11
  z = dcmplx(x, x)
  if (z / z /= dcmplx(1.0d0, 0.0d0)) error stop 12
  q = dcmplx(x, 0.0d0)
  z = dcmplx(0.0d0, x)
  if (z / q /= dcmplx(0.0d0, 1.0d0)) error stop 13
  if (q / z /= dcmplx(0.0d0, -1.0d0)) error stop 14
  q = dcmplx(x, -x)
  if (z / q /= dcmplx(-0.5d0, 0.5d0)) error stop 15

  status = 0
  call validate_complex(-cmplx(1.0, 2.0), status)
  if (status /= 1) error stop 16
end program complex_arithmetic

subroutine validate_complex(value, status)
  implicit none
  complex :: value
  integer :: status
  if (value == cmplx(-1.0, -2.0)) status = 1
end subroutine validate_complex
