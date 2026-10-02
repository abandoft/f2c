program process_cpu_time
  implicit none
  intrinsic :: etime
  ! GNU 16.1 otherwise folds SUM to the pre-ETIME fill despite the output writes.
  ! VOLATILE preserves the native oracle and exercises qualified output stores.
  real, volatile :: values(-3:0)
  real, volatile :: f2c_zero_index(2), f2c_zero_index_0(2)
  real :: total
  values = 27.0
  total = etime(values)
  if (transfer(values(-1), 0) /= transfer(27.0, 0) .or. &
      transfer(values(0), 0) /= transfer(27.0, 0)) stop 1
  if (total < 0.0) then
    if (transfer(total, 0) /= transfer(-1.0, 0) .or. &
        transfer(values(-3), 0) /= transfer(-1.0, 0) .or. &
        transfer(values(-2), 0) /= transfer(-1.0, 0)) stop 2
  else
    if (any(values(-3:-2) < 0.0)) stop 3
    if (abs(total - sum(values(-3:-2))) > 0.000001) stop 4
  end if
  f2c_zero_index = 13.0
  f2c_zero_index_0 = 17.0
  total = etime(f2c_zero_index)
  if (transfer(f2c_zero_index_0(1), 0) /= transfer(17.0, 0) .or. &
      transfer(f2c_zero_index_0(2), 0) /= transfer(17.0, 0)) stop 5
  print '(a)', 'ETIME property contracts passed'
end program
