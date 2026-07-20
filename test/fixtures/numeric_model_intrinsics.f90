program numeric_model_intrinsics
  implicit none
  integer(1) :: i1
  integer(2) :: i2
  integer(8) :: i8
  integer :: integer_range_request, precision_request, range_request, radix_request
  integer, external :: touch_integer
  real, external :: touch_real
  double precision, external :: touch_double
  complex, external :: touch_complex

  i1 = 0_1
  i2 = 0_2
  i8 = 0_8

  if (digits(i1) /= 7 .or. digits(i2) /= 15 .or. digits(i8) /= 63) error stop 1
  if (huge(i1) /= 127_1 .or. huge(i2) /= 32767_2) error stop 2
  if (huge(i8) /= 9223372036854775807_8) error stop 3
  if (radix(touch_integer()) /= 2 .or. digits(touch_integer()) /= 31) error stop 4
  if (range(touch_integer()) /= 9 .or. kind(touch_integer()) /= 4) error stop 5
  if (huge(touch_integer()) /= 2147483647) error stop 6

  if (digits(touch_real()) /= 24 .or. radix(touch_real()) /= 2) error stop 7
  if (minexponent(touch_real()) /= -125 .or. maxexponent(touch_real()) /= 128) error stop 8
  if (precision(touch_real()) /= 6 .or. range(touch_real()) /= 37) error stop 9
  if (epsilon(touch_real()) /= 1.1920928955078125e-7) error stop 10
  if (tiny(touch_real()) /= 1.1754943508222875e-38) error stop 11
  if (huge(touch_real()) /= 3.4028234663852886e38) error stop 12

  if (digits(touch_double()) /= 53 .or. radix(touch_double()) /= 2) error stop 13
  if (minexponent(touch_double()) /= -1021 .or. maxexponent(touch_double()) /= 1024) error stop 14
  if (precision(touch_double()) /= 15 .or. range(touch_double()) /= 307) error stop 15
  if (epsilon(touch_double()) /= 2.2204460492503131d-16) error stop 16
  if (tiny(touch_double()) /= 2.2250738585072014d-308) error stop 17
  if (huge(touch_double()) /= 1.7976931348623157d308) error stop 18

  if (precision(touch_complex()) /= 6 .or. range(touch_complex()) /= 37) error stop 19
  if (kind('f2c') /= 1 .or. kind(.true.) /= 4) error stop 20

  if (selected_int_kind(2) /= 1 .or. selected_int_kind(3) /= 2) error stop 21
  if (selected_int_kind(9) /= 4 .or. selected_int_kind(18) /= 8) error stop 22
  if (selected_real_kind(6) /= 4 .or. selected_real_kind(7) /= 8) error stop 24

  integer_range_request = 18
  precision_request = 6
  range_request = 37
  radix_request = 2
  if (selected_int_kind(integer_range_request) /= 8) error stop 29
  if (selected_real_kind(radix=radix_request, r=range_request, p=precision_request) /= 4) &
    error stop 30

  print '(A,1X,10(I0,1X))', 'NUMERIC-MODEL', digits(i1), digits(i2), digits(i8), &
    precision(touch_real()), precision(touch_double()), range(touch_integer()), &
    range(touch_real()), range(touch_double()), selected_int_kind(integer_range_request), &
    selected_real_kind(p=precision_request, r=range_request, radix=radix_request)
end program numeric_model_intrinsics
