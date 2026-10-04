program loop_static_flat
  use iso_fortran_env, only: int64
  implicit none
  integer(int64) :: wide, values(3)
  ! GNU Fortran 16.1.0 produces 0, 1, 2 here, both at O0 and O2.
  ! Keep exact standard assertions separate from that native folding path.
  wide = 77_int64
  values = [(wide,wide=4294967296_int64,4294967298_int64)]
  if (wide /= 77_int64) stop 1
  if (any(values /= [4294967296_int64,4294967297_int64,4294967298_int64])) stop 2
  print '(A)', 'static flat wide constructor passed'
end program
