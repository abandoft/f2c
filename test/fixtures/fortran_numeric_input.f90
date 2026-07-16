program fortran_numeric_input
  implicit none
  double precision :: value
  double complex :: pair
  read(*,*) value
  read(*,*) pair
  if (abs(value - 100.0d0) > 1.0d-12) error stop
  if (abs(real(pair, kind=8) - 2.0d0) > 1.0d-12) error stop
  if (abs(aimag(pair) + 3.0d0) > 1.0d-12) error stop
end program fortran_numeric_input
