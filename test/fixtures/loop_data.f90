program loop_data
  use iso_fortran_env, only: int8, int64
  implicit none
  integer(int64), parameter :: first = 4294967296_int64, last = first+2_int64
  integer(int64) :: wide, inner, values(3), matrix(2,2), runtime_first, runtime_last
  integer(int8) :: small, small_values(12)
  integer :: empty(0), counter
  data (values(wide-first+1_int64),wide=first,last) / 0*99,21,0*99,22,23,0*99 /
  data ((matrix(wide-first+1_int64,inner),wide=first,first+1_int64),inner=1_int64,2_int64) / &
    0*0,1,2,0*0,3,4,0*0 /
  data (small_values((int(small,int64)+100_int64)/17_int64+1_int64), &
         small=-100_int8,100_int8,17_int8) / 12*7_int8 /
  data empty / 0*1,0*2 /
  data counter / 0*0,17,0*0 /
  if (any(values /= [21_int64,22_int64,23_int64])) stop 1
  if (any(matrix(:,1) /= [1_int64,2_int64]) .or. &
      any(matrix(:,2) /= [3_int64,4_int64])) stop 2
  if (any(small_values /= 7_int8)) stop 3
  if (size(empty) /= 0 .or. counter /= 17) stop 4
  wide = 77_int64
  runtime_first = first
  runtime_last = last
  values = [(wide,wide=runtime_first,runtime_last)]
  if (wide /= 77_int64 .or. any(values /= &
      [4294967296_int64,4294967297_int64,4294967298_int64])) stop 5
  print '(A)', 'DATA loop and zero repetition semantics passed'
end program
