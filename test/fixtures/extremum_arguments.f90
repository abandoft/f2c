program extremum_arguments
  implicit none
  integer :: calls, answer, values(3), output(3)
  integer, parameter :: folded = max( &
    1, 2, 3, 4, 5, 6, &
    7, 8, 9, 10, 11, 12, &
    13, 14, 15, 16, 17, 18, &
    19, 20, 21, 22, 23, 24, &
    25, 26, 27, 28, 29, 30, &
    31, 32, 33, 34, 35, 36, &
    37, 38, 39, 40, 41, 42, &
    43, 44, 45, 46, 47, 48, &
    49, 50, 51, 52, 53, 54, &
    55, 56, 57, 58, 59, 60, &
    61, 62, 63, 64, 65, 66, &
    67, 68, 69, 70, 71, 72)
  real(kind=8) :: wide(3), wide_output(3)
  if (folded /= 72) stop 1
  calls = 0
  answer = max( &
    produce(), produce(), produce(), produce(), produce(), produce(), &
    produce(), produce(), produce(), produce(), produce(), produce(), &
    produce(), produce(), produce(), produce(), produce(), produce(), &
    produce(), produce(), produce(), produce(), produce(), produce(), &
    produce(), produce(), produce(), produce(), produce(), produce(), &
    produce(), produce(), produce(), produce(), produce(), produce(), &
    produce(), produce(), produce(), produce(), produce(), produce(), &
    produce(), produce(), produce(), produce(), produce(), produce(), &
    produce(), produce(), produce(), produce(), produce(), produce(), &
    produce(), produce(), produce(), produce(), produce(), produce(), &
    produce(), produce(), produce(), produce(), produce(), produce(), &
    produce(), produce(), produce(), produce(), produce(), produce())
  if (answer /= 5 .or. calls /= 72) stop 2
  values = [7,-2,3]
  output = min( &
    values, values, values, values, values, values, &
    values, values, values, values, values, values, &
    values, values, values, values, values, values, &
    values, values, values, values, values, values, &
    values, values, values, values, values, values, &
    values, values, values, values, values, values, &
    values, values, values, values, values, values, &
    values, values, values, values, values, values, &
    values, values, values, values, values, values, &
    values, values, values, values, values, values, &
    values, values, values, values, values, values, &
    values, values, values, values, values, values)
  if (any(output /= values)) stop 3
  answer = max(a65=values(1),a1=values(2),a2=values(3))
  if (answer /= 7) stop 4
  answer = min(values(1),a1000=values(2),a2=values(3))
  if (answer /= -2) stop 5
  output = max(a65=values(3:1:-1),a2=values,a1=-1)
  if (any(output /= [7,-1,7])) stop 6
  wide = [7.0_8,-2.0_8,3.0_8]
  wide_output = min(a1000=wide(3:1:-1),a1=5.0_8,a2=wide)
  if (any(wide_output /= [3.0_8,-2.0_8,3.0_8])) stop 7
  if (amax0(a65=values(1),a2=values(3),a1=values(2)) /= 7.0) stop 8
  print '(A)', 'extremum argument differential passed'
contains
  function produce() result(result)
    integer :: result
    calls = calls + 1
    result = 5
  end function produce
end program extremum_arguments
