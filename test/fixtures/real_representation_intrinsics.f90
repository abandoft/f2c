program real_representation_intrinsics
  implicit none

  real :: sub4
  real :: values4(8), fractions4(8), spacings4(8)
  real :: rr_values4(7), rr_results4(7)
  real :: nearest_values4(6), nearest_directions4(6), nearest_results4(6)
  real :: scale_values4(4), scale_results4(4), set_values4(4), set_results4(4)
  integer :: exponents4(8)
  integer(kind=8) :: powers4(4)

  double precision :: sub8
  double precision :: values8(8), fractions8(8), spacings8(8)
  double precision :: rr_values8(7), rr_results8(7)
  double precision :: nearest_values8(6), nearest_directions8(6), nearest_results8(6)
  double precision :: scale_values8(4), scale_results8(4), set_values8(4), set_results8(4)
  integer :: exponents8(8)
  integer(kind=8) :: powers8(4)
  integer(kind=8) :: huge_power

  sub4 = nearest(0.0, 1.0)
  values4 = [0.0, -0.0, 0.5, 1.0, 2.0, tiny(0.0), sub4, huge(0.0)]
  exponents4 = exponent(x=values4)
  if (any(exponents4 /= [0, 0, 0, 1, 2, -125, -148, 128])) error stop 1

  fractions4 = fraction(x=values4)
  if (any(fractions4 /= [0.0, -0.0, 0.5, 0.5, 0.5, 0.5, 0.5, &
                         nearest(1.0, -1.0)])) error stop 2

  spacings4 = spacing(x=values4)
  if (any(spacings4 /= [tiny(0.0), tiny(0.0), epsilon(0.0) / 2.0, epsilon(0.0), &
                        2.0 * epsilon(0.0), tiny(0.0), tiny(0.0), &
                        scale(1.0, maxexponent(0.0) - digits(0.0))])) error stop 3

  rr_values4 = [0.0, 0.5, 1.0, 1.5, tiny(0.0), sub4, -2.0]
  rr_results4 = rrspacing(x=rr_values4)
  if (any(rr_results4 /= [0.0, 8388608.0, 8388608.0, 12582912.0, &
                           8388608.0, 8388608.0, 8388608.0])) error stop 4

  nearest_values4 = [0.0, 0.0, 1.0, 1.0, -1.0, -1.0]
  nearest_directions4 = [1.0, -1.0, 1.0, -1.0, 1.0, -1.0]
  nearest_results4 = nearest(x=nearest_values4, s=nearest_directions4)
  if (any(nearest_results4 /= [sub4, -sub4, 1.0 + epsilon(0.0), &
                               1.0 - epsilon(0.0) / 2.0, -1.0 + epsilon(0.0) / 2.0, &
                               -1.0 - epsilon(0.0)])) error stop 5

  scale_values4 = [0.75, -0.75, tiny(0.0), sub4]
  powers4 = [2_8, -2_8, 1_8, 1_8]
  scale_results4 = scale(i=powers4, x=scale_values4)
  if (any(scale_results4 /= [3.0, -0.1875, 2.0 * tiny(0.0), 2.0 * sub4])) error stop 6

  set_values4 = [6.0, -6.0, 0.0, -0.0]
  set_results4 = set_exponent(i=powers4, x=set_values4)
  if (any(set_results4 /= [3.0, -0.1875, 0.0, -0.0])) error stop 7

  sub8 = nearest(0.0d0, 1.0d0)
  values8 = [0.0d0, -0.0d0, 0.5d0, 1.0d0, 2.0d0, tiny(0.0d0), sub8, huge(0.0d0)]
  exponents8 = exponent(x=values8)
  if (any(exponents8 /= [0, 0, 0, 1, 2, -1021, -1073, 1024])) error stop 8

  fractions8 = fraction(x=values8)
  if (any(fractions8 /= [0.0d0, -0.0d0, 0.5d0, 0.5d0, 0.5d0, 0.5d0, 0.5d0, &
                         nearest(1.0d0, -1.0d0)])) error stop 9

  spacings8 = spacing(x=values8)
  if (any(spacings8 /= [tiny(0.0d0), tiny(0.0d0), epsilon(0.0d0) / 2.0d0, &
                        epsilon(0.0d0), 2.0d0 * epsilon(0.0d0), tiny(0.0d0), tiny(0.0d0), &
                        scale(1.0d0, maxexponent(0.0d0) - digits(0.0d0))])) error stop 10

  rr_values8 = [0.0d0, 0.5d0, 1.0d0, 1.5d0, tiny(0.0d0), sub8, -2.0d0]
  rr_results8 = rrspacing(x=rr_values8)
  if (any(rr_results8 /= [0.0d0, 4503599627370496.0d0, 4503599627370496.0d0, &
                           6755399441055744.0d0, 4503599627370496.0d0, &
                           4503599627370496.0d0, 4503599627370496.0d0])) error stop 11

  nearest_values8 = [0.0d0, 0.0d0, 1.0d0, 1.0d0, -1.0d0, -1.0d0]
  nearest_directions8 = [1.0d0, -1.0d0, 1.0d0, -1.0d0, 1.0d0, -1.0d0]
  nearest_results8 = nearest(x=nearest_values8, s=nearest_directions8)
  if (any(nearest_results8 /= [sub8, -sub8, 1.0d0 + epsilon(0.0d0), &
                               1.0d0 - epsilon(0.0d0) / 2.0d0, &
                               -1.0d0 + epsilon(0.0d0) / 2.0d0, &
                               -1.0d0 - epsilon(0.0d0)])) error stop 12

  scale_values8 = [0.75d0, -0.75d0, tiny(0.0d0), sub8]
  powers8 = [2_8, -2_8, 1_8, 1_8]
  scale_results8 = scale(i=powers8, x=scale_values8)
  if (any(scale_results8 /= [3.0d0, -0.1875d0, 2.0d0 * tiny(0.0d0), &
                             2.0d0 * sub8])) error stop 13

  set_values8 = [6.0d0, -6.0d0, 0.0d0, -0.0d0]
  set_results8 = set_exponent(i=powers8, x=set_values8)
  if (any(set_results8 /= [3.0d0, -0.1875d0, 0.0d0, -0.0d0])) error stop 14

  huge_power = huge(0_8)
  if (scale(0.0, huge_power) /= 0.0) error stop 15
  if (scale(-0.0d0, -huge_power) /= -0.0d0) error stop 16
  if (set_exponent(0.0, huge_power) /= 0.0) error stop 17
  if (set_exponent(-0.0d0, -huge_power) /= -0.0d0) error stop 18

  print '(A)', 'REAL-REPRESENTATION-PASS'
end program real_representation_intrinsics
