program reduction_intrinsics
  implicit none
  integer :: values(5)
  logical :: flags(5)

  values = [2, 3, 4, 5, 6]
  flags = [.true., .false., .true., .true., .false.]

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
end program reduction_intrinsics
