program loop_names
  use iso_fortran_env, only: int64
  implicit none
  integer :: index, total
  integer :: f2c_do_start_22, f2c_do_start_26, f2c_do_start_6, f2c_do_start_7
  integer :: f2c_do_start_8, f2c_do_start_9, f2c_do_start_10, f2c_do_start_11
  integer :: F2C_LOOP_BEGIN, F2C_LOOP_I32
  integer(int64) :: wide, first, last, values(3)
  integer(int64) :: f2c_io_do_start_0, f2c_constructor_value_0
  character(len=128) :: record
  f2c_do_start_22 = 4
  f2c_do_start_26 = 5
  f2c_do_start_6 = 6
  f2c_do_start_7 = 7
  f2c_do_start_8 = 8
  f2c_do_start_9 = 9
  f2c_do_start_10 = 10
  f2c_do_start_11 = 11
  F2C_LOOP_BEGIN = 12
  F2C_LOOP_I32 = 13
  f2c_io_do_start_0 = 14_int64
  f2c_constructor_value_0 = 15_int64
  total = 0
  do index = 1, 3
    total = total + index
  end do
  if (total /= 6 .or. index /= 4) stop 1
  do index = 4, 1, -1
    total = total + index
  end do
  if (total /= 16 .or. index /= 0) stop 2
  first = 4294967296_int64
  last = first + 2_int64
  values = [(wide, wide = first, last)]
  if (any(values /= [4294967296_int64, 4294967297_int64, 4294967298_int64])) stop 3
  write(record, '(3(I12,1X))') (wide, wide = first, last)
  if (wide /= first+3_int64) stop 4
  if (f2c_io_do_start_0 /= 14_int64 .or. f2c_constructor_value_0 /= 15_int64) stop 5
  if (F2C_LOOP_BEGIN /= 12 .or. F2C_LOOP_I32 /= 13) stop 6
  if (f2c_do_start_22 /= 4 .or. f2c_do_start_26 /= 5 .or. f2c_do_start_6 /= 6 .or. &
      f2c_do_start_7 /= 7 .or. f2c_do_start_8 /= 8 .or. f2c_do_start_9 /= 9 .or. &
      f2c_do_start_10 /= 10 .or. f2c_do_start_11 /= 11) stop 7
  print '(A)', 'loop implementation names passed'
end program
