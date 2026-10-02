program process_cpu_time
  implicit none
  intrinsic :: etime
  real :: values(-3:0), total
  values = 27.0
  total = etime(values)
  if (any(values(-1:0) /= 27.0)) stop 1
  if (total < 0.0) then
    if (total /= -1.0 .or. any(values(-3:-2) /= -1.0)) stop 2
  else
    if (any(values(-3:-2) < 0.0)) stop 3
    if (abs(total - sum(values(-3:-2))) > 0.000001) stop 4
  end if
  print '(a)', 'ETIME property contracts passed'
end program
