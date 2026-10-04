! NaN and signed-zero ties exercise the documented f2c processor policy.
! They are not compared against a different processor's unspecified policy.
program extremum_policy
  implicit none
  real, volatile :: nan4, inf4, negative4, positive4, values4(4), matrix4(2,2)
  real(kind=8), volatile :: nan8, inf8, negative8, positive8, values8(4), matrix8(2,2)
  logical(kind=1), volatile :: selected(4)
  real :: output4(2), folded_max, folded_min
  real(kind=8) :: output8(2), wide_max, wide_min
  integer :: locations(2), calls

  nan4 = transfer(2143289344, 0.0)
  inf4 = transfer(2139095040, 0.0)
  negative4 = transfer(-2147483647-1, 0.0)
  positive4 = 0.0
  nan8 = transfer(9221120237041090560_8, 0.0_8)
  inf8 = transfer(9218868437227405312_8, 0.0_8)
  negative8 = transfer(-9223372036854775807_8-1_8, 0.0_8)
  positive8 = 0.0_8
  if (max(nan4, 3.0) == max(nan4, 3.0) .or. max(3.0, nan4) == max(3.0, nan4)) stop 1
  if (min(nan8, 3.0_8) == min(nan8, 3.0_8) .or. min(3.0_8, nan8) == min(3.0_8, nan8)) stop 2
  if (max(nan8, nan8) == max(nan8, nan8)) stop 3
  if (min(nan4, nan4) == min(nan4, nan4)) stop 4
  if (transfer(max(negative4, positive4), 0) /= 0) stop 5
  if (transfer(max(positive4, negative4), 0) /= 0) stop 6
  if (transfer(min(negative8, positive8), 0_8) /= -9223372036854775807_8-1_8) stop 7
  if (transfer(min(positive8, negative8), 0_8) /= -9223372036854775807_8-1_8) stop 8
  if (transfer(max(negative4, negative4), 0) /= -2147483647-1) stop 9
  if (transfer(min(positive8, positive8), 0_8) /= 0_8) stop 10
  folded_max = max(-0.0, 0.0)
  folded_min = min(0.0, -0.0)
  wide_max = max(0.0_8, -0.0_8)
  wide_min = min(-0.0_8, 0.0_8)
  if (transfer(folded_max, 0) /= 0 .or. transfer(wide_max, 0_8) /= 0_8) stop 11
  if (transfer(folded_min, 0) /= -2147483647-1) stop 12
  if (transfer(wide_min, 0_8) /= -9223372036854775807_8-1_8) stop 13

  values4 = -inf4
  values8 = inf8
  selected = [.false., .true., .true., .false.]
  if (maxval(values4) /= -inf4) stop 14
  if (minval(values8) /= inf8) stop 15
  values4 = [nan4, -inf4, -inf4, nan4]
  values8 = [nan8, inf8, inf8, nan8]
  if (maxval(values4) == maxval(values4)) stop 42
  if (minval(values8) == minval(values8)) stop 43
  if (maxval(values4, mask=selected) /= -inf4) stop 16
  if (minval(values8(4:1:-1), mask=selected) /= inf8) stop 17
  if (maxloc(values4, dim=1) /= 1) stop 18
  if (maxloc(values4, dim=1, back=.true.) /= 4) stop 19
  if (minloc(values8, dim=1, mask=selected) /= 2) stop 20
  if (minloc(values8, dim=1, back=.true., kind=8) /= 4_8) stop 21
  matrix4 = reshape(values4, [2,2])
  matrix8 = reshape(values8, [2,2])
  output4 = maxval(matrix4, dim=1)
  output8 = minval(matrix8, dim=2)
  if (any(output4 == output4) .or. any(output8 == output8)) stop 22
  locations = maxloc(matrix4, back=.true.)
  if (any(locations /= [2,2])) stop 23
  locations = minloc(matrix8, dim=2, back=.true.)
  if (any(locations /= [1,2])) stop 24
  values4 = nan4
  values8 = nan8
  if (maxval(values4) == maxval(values4)) stop 25
  if (minval(values8, mask=selected) == minval(values8, mask=selected)) stop 26
  if (maxloc(values4, dim=1) /= 1) stop 27
  if (minloc(values8, dim=1, back=.true.) /= 4) stop 28
  matrix4 = nan4
  matrix8 = nan8
  output4 = maxval(matrix4, dim=2)
  output8 = minval(matrix8, dim=1)
  if (any(output4 == output4) .or. any(output8 == output8)) stop 29
  locations = maxloc(matrix4, dim=1, back=.true.)
  if (any(locations /= [2,2])) stop 30

  values4 = [negative4, positive4, negative4, positive4]
  values8 = [positive8, negative8, positive8, negative8]
  if (transfer(maxval(values4), 0) /= 0) stop 31
  if (transfer(minval(values8), 0_8) /= -9223372036854775807_8-1_8) stop 32
  if (maxloc(values4, dim=1) /= 1) stop 33
  if (minloc(values8, dim=1, back=.true.) /= 4) stop 34
  output4 = max([nan4, negative4], [3.0, positive4])
  if (output4(1) == output4(1) .or. transfer(output4(2), 0) /= 0) stop 35
  output8 = min([positive8, nan8], [negative8, 3.0_8])
  if (transfer(output8(1), 0_8) /= -9223372036854775807_8-1_8) stop 36
  if (output8(2) == output8(2)) stop 37
  calls = 0
  wide_max = max(produce(), 3.0_8, produce())
  if (wide_max == wide_max) stop 38
  if (calls /= 2) stop 39
  calls = 0
  wide_max = maxval(produce_array())
  if (wide_max == wide_max) stop 40
  if (calls /= 1) stop 41
  print '(A)', 'extremum processor policy passed'
contains
  function produce() result(result)
    real(kind=8) :: result
    calls = calls + 1
    result = nan8
  end function produce
  function produce_array() result(result)
    real(kind=8) :: result(2)
    calls = calls + 1
    result = [nan8, -inf8]
  end function produce_array
end program extremum_policy
