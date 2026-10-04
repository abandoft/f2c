program loop_static_constructor
  use iso_fortran_env, only: int64
  implicit none
  integer(int64) :: wide, inner, values(4)
  ! GNU Fortran 16 rejects constant expansion of this valid wide nested case.
  ! Retain an independent exact-value oracle instead of disabling its range checks.
  values = [((wide + inner, inner = 1_int64, 2_int64), &
              wide = 4294967296_int64, 4294967298_int64, 2_int64)]
  if (any(values /= [4294967297_int64, 4294967298_int64, &
                     4294967299_int64, 4294967300_int64])) stop 1
  print '(A)', 'static wide constructor passed'
end program
