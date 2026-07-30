program reduction_intrinsics
  implicit none
  integer :: values(5)
  logical :: flags(5)
  integer(kind=8) :: wide_values(2)
  real :: real_values(2)
  complex :: complex_values(2), complex_weights(2)
  complex(kind=8) :: wide_complex_values(2), wide_complex_weights(2)
  logical(kind=1) :: compact_flags(3)
  logical(kind=8) :: wide_flags(3)
  integer :: tied_values(4)
  logical(kind=1) :: selected(4)

  values = [2, 3, 4, 5, 6]
  flags = [.true., .false., .true., .true., .false.]
  wide_values = [2_8, 3_8]
  real_values = [4.0, 5.0]
  complex_values = [(1.0, 2.0), (3.0, 4.0)]
  complex_weights = [(5.0, 6.0), (7.0, 8.0)]
  wide_complex_values = [(1.0_8, 2.0_8), (3.0_8, 4.0_8)]
  wide_complex_weights = [(5.0_8, 6.0_8), (7.0_8, 8.0_8)]
  compact_flags = [.true., .false., .true.]
  wide_flags = [.false., .true., .true.]
  tied_values = [2, 9, 9, 4]
  selected = [.false., .true., .true., .false.]

  if (sum(values) /= 20) stop 1
  if (sum(values(5:1:-2)) /= 12) stop 2
  if (product(values(2:4)) /= 60) stop 3
  if (maxval(values(1:5:2)) /= 6) stop 4
  if (minval(values) /= 2) stop 5
  if (maxloc(values, dim=1) /= 5) stop 6
  if (minloc(values, dim=1) /= 1) stop 7
  if (count(flags) /= 3) stop 8
  if (.not. any(flags)) stop 9
  if (all(flags)) stop 10
  if (dot_product(values, [1, 2, 3, 4, 5]) /= 70) stop 11
  if (dot_product(wide_values, real_values) /= 23.0) stop 12
  if (sum(complex_values) /= (4.0, 6.0)) stop 13
  if (product(complex_values) /= (-5.0, 10.0)) stop 14
  if (dot_product(complex_values, complex_weights) /= (70.0, -8.0)) stop 15
  if (dot_product(wide_complex_values, wide_complex_weights) /= (70.0_8, -8.0_8)) stop 16
  if (.not. dot_product(compact_flags, wide_flags)) stop 17
  if (sum(tied_values, mask=selected) /= 18) stop 18
  if (product(tied_values, mask=selected) /= 81) stop 19
  if (maxval(tied_values, mask=selected) /= 9) stop 20
  if (minval(tied_values, mask=selected) /= 9) stop 21
  if (sum(tied_values, mask=.false.) /= 0) stop 22
  if (product(tied_values, mask=.false.) /= 1) stop 23
  if (maxloc(tied_values, dim=1, mask=selected, kind=8) /= 2_8) stop 24
  if (maxloc(tied_values, dim=1, mask=selected, back=.true.) /= 3) stop 25
  if (minloc(tied_values, dim=1, mask=selected, back=.true.) /= 3) stop 26
  if (count(selected, kind=1) /= 2_1) stop 27
  if (sum(complex_values, mask=[.true., .false.]) /= (1.0, 2.0)) stop 28
  print '(A)', 'reduction intrinsic differential passed'
end program reduction_intrinsics
