program loop_real_controls
  use iso_fortran_env, only: int8, int64, real64
  implicit none
  integer(int8) :: small
  integer(int64) :: wide, total
  real(real64) :: first, last, stride
  first = 1.9_real64
  last = 3.9_real64
  stride = 1.9_real64
  total = 0_int64
  do small=first,last,stride
    total = total + small
  end do
  if (total /= 6_int64 .or. small /= 4_int8) stop 1
  first = -3.9_real64
  last = -1.9_real64
  total = 0_int64
  do small=first,last,stride
    total = total + small
  end do
  if (total /= -6_int64 .or. small /= 0_int8) stop 2
  first = 4294967296.75_real64
  last = 4294967298.75_real64
  total = 0_int64
  do wide=first,last,stride
    total = total + wide
  end do
  if (total /= 12884901891_int64 .or. wide /= 4294967299_int64) stop 3
  print '(A)', 'legacy real-to-integer loop controls passed'
end program
