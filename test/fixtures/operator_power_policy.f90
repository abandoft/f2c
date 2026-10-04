! Two's-complement boundary policy: deliberately separate from the standard
! Fortran native oracle, whose integer model has a symmetric range.
program operator_power_policy
  implicit none
  integer(kind=1) :: narrow, narrow_exponent
  integer(kind=8) :: base, exponent, minimum
  real(kind=8) :: real_base
  complex(kind=8) :: complex_base
  narrow = -2_1
  narrow_exponent = 7_1
  if (int(narrow ** narrow_exponent,8) /= -128_8) stop 1
  minimum = -huge(0_8) - 1_8
  base = -2_8
  exponent = 63_8
  if (base ** exponent /= minimum) stop 2
  base = minimum
  exponent = 1_8
  if (base ** exponent /= minimum) stop 3
  base = -1_8
  exponent = minimum
  if (base ** exponent /= 1_8) stop 4
  base = -2_8
  if (base ** exponent /= 0_8) stop 5
  real_base = -1.0_8
  if (abs(real_base ** exponent - 1.0_8) > 0.0_8) stop 6
  complex_base = (-1.0_8,0.0_8)
  if (abs(complex_base ** exponent - (1.0_8,0.0_8)) > 0.0_8) stop 7
  write(*,'(A)') 'operator processor policy passed'
end program
