program data_statement
  implicit none
  integer, parameter :: repeat = 2
  integer :: matrix(2,2), vector(2), mixed(2), partial(3), tail, i, j
  character(len=8) :: label
  complex :: values(2)

  data ((matrix(i,j), i=1,2), j=1,2) / 1, 2, 3, 4 /
  data vector / repeat*5 /
  data mixed, tail / 6, 7, 8 /
  data label / 'a/b,c' /
  data values / (1.0,2.0), (3.0,4.0) /
  data partial(2) / 9 /

  if (any(vector /= [5, 5])) stop 1
  if (any(matrix(:,1) /= [1, 2])) stop 2
  if (any(matrix(:,2) /= [3, 4])) stop 3
  if (any(mixed /= [6, 7]) .or. tail /= 8) stop 4
  if (label /= 'a/b,c') stop 5
  if (real(values(1)) /= 1.0 .or. aimag(values(1)) /= 2.0) stop 6
  if (real(values(2)) /= 3.0 .or. aimag(values(2)) /= 4.0) stop 7
  if (next_value() /= 11) stop 8
  if (next_value() /= 12) stop 9
  if (next_array_value() /= 11) stop 10
  if (next_array_value() /= 12) stop 11
  if (next_complex_value() /= 1.0) stop 12
  if (next_complex_value() /= 2.0) stop 13
  if (next_intrinsic_value() /= 10) stop 14
  if (next_intrinsic_value() /= 11) stop 15
  if (partial(2) /= 9) stop 16
  if (next_equivalence_value() /= 11) stop 17
  if (next_equivalence_value() /= 12) stop 18

contains

  integer function next_value()
    implicit none
    integer :: counter
    data counter / 10 /
    counter = counter + 1
    next_value = counter
  end function next_value

  integer function next_array_value()
    implicit none
    integer :: counters(2)
    data counters / 10, 20 /
    counters(1) = counters(1) + 1
    next_array_value = counters(1)
  end function next_array_value

  real function next_complex_value()
    implicit none
    complex :: state
    data state / (1.0, 2.0) /
    next_complex_value = real(state)
    state = state + (1.0, 0.0)
  end function next_complex_value

  integer function next_intrinsic_value()
    implicit none
    integer, parameter :: initial = abs(-9)
    integer :: state
    data state / initial /
    state = state + 1
    next_intrinsic_value = state
  end function next_intrinsic_value

  integer function next_equivalence_value()
    implicit none
    integer :: storage(2), value
    equivalence (storage(2), value)
    data value / 10 /
    value = value + 1
    next_equivalence_value = value
  end function next_equivalence_value
end program data_statement
