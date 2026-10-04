program extremum_boundaries
  implicit none
  real, volatile :: empty4(0), values4(4), matrix4(0,2)
  real(kind=8), volatile :: empty8(0), values8(4), matrix8(2,0)
  integer(kind=1), volatile :: empty_i1(0), values_i1(2)
  integer(kind=2), volatile :: empty_i2(0), values_i2(2)
  integer(kind=4), volatile :: empty_i4(0), values_i4(2)
  integer(kind=8), volatile :: empty_i8(0), values_i8(2)
  logical(kind=1), volatile :: none1(4)
  logical(kind=8), volatile :: none8(4)
  real :: columns4(2)
  real(kind=8) :: columns8(2)
  integer :: locations(2), calls

  values4 = [1.0, -huge(0.0), huge(0.0), -1.0]
  values8 = [1.0_8, -huge(0.0_8), huge(0.0_8), -1.0_8]
  none1 = .false.
  none8 = .false.
  if (maxval(empty4) /= -huge(0.0)) stop 1
  if (minval(empty4) /= huge(0.0)) stop 2
  if (maxval(empty8) /= -huge(0.0_8)) stop 3
  if (minval(empty8) /= huge(0.0_8)) stop 4
  if (maxval(values4, mask=.false.) /= -huge(0.0)) stop 5
  if (minval(values8, mask=.false.) /= huge(0.0_8)) stop 6
  if (maxval(values4, mask=none8) /= -huge(0.0)) stop 7
  if (minval(values4, mask=none1) /= huge(0.0)) stop 8
  if (maxval(values8, mask=none1) /= -huge(0.0_8)) stop 9
  if (minval(values8, mask=none8) /= huge(0.0_8)) stop 10
  if (maxval(values4(4:1:-1)) /= huge(0.0)) stop 11
  if (minval(values8(4:1:-1)) /= -huge(0.0_8)) stop 12
  if (maxval(values4(2:2)) /= -huge(0.0)) stop 13
  if (minval(values8(3:3)) /= huge(0.0_8)) stop 14

  columns4 = maxval(matrix4, dim=1)
  if (any(columns4 /= -huge(0.0))) stop 15
  columns4 = minval(matrix4, dim=1, mask=.false.)
  if (any(columns4 /= huge(0.0))) stop 16
  columns8 = maxval(matrix8, dim=2)
  if (any(columns8 /= -huge(0.0_8))) stop 17
  columns8 = minval(matrix8, dim=2, mask=.false.)
  if (any(columns8 /= huge(0.0_8))) stop 18
  locations = maxloc(matrix4)
  if (any(locations /= 0)) stop 19
  locations = minloc(matrix8, back=.true.)
  if (any(locations /= 0)) stop 20
  if (maxloc(empty4, dim=1) /= 0) stop 21
  if (minloc(values8, dim=1, mask=none8, back=.true.) /= 0) stop 22

  values_i1 = -huge(0_1)
  values_i2 = -huge(0_2)
  values_i4 = -huge(0_4)
  values_i8 = -huge(0_8)
  values_i1 = values_i1 - 1_1
  values_i2 = values_i2 - 1_2
  values_i4 = values_i4 - 1_4
  values_i8 = values_i8 - 1_8
  if (maxval(empty_i1) /= values_i1(1)) stop 23
  if (maxval(empty_i2) /= values_i2(1)) stop 24
  if (maxval(empty_i4) /= values_i4(1)) stop 25
  if (maxval(empty_i8) /= values_i8(1)) stop 26
  if (minval(empty_i1) /= huge(0_1)) stop 27
  if (minval(empty_i2) /= huge(0_2)) stop 28
  if (minval(empty_i4) /= huge(0_4)) stop 29
  if (minval(empty_i8) /= huge(0_8)) stop 30
  if (maxval(values_i1) /= values_i1(1)) stop 31
  if (maxval(values_i2, mask=[.true.,.false.]) /= values_i2(1)) stop 32
  if (minval(values_i4) /= values_i4(1)) stop 33
  if (minval(values_i8, mask=.true.) /= values_i8(1)) stop 34
  if (maxloc(values_i1, dim=1) /= 1) stop 35
  if (minloc(values_i8, dim=1, back=.true., kind=8) /= 2_8) stop 36
  calls = 0
  if (maxval(produce_empty()) /= -huge(0.0_8)) stop 37
  if (calls /= 1) stop 38
  print '(A)', 'extremum boundary differential passed'
contains
  function produce_empty() result(result)
    real(kind=8) :: result(0)
    calls = calls + 1
    result = 0.0_8
  end function produce_empty
end program extremum_boundaries
