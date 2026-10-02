module intrinsic_provider
  implicit none
  intrinsic :: max, sqrt, conjg, cpu_time
  intrinsic :: min
  public :: max, min, sqrt, conjg, cpu_time
contains
  subroutine check_host_intrinsics
    if (max(2, 5) /= 5) stop 1
    if (min(2, 5) /= 2) stop 2
    call nested
  contains
    subroutine nested
      if (abs(sqrt(4.0) - 2.0) > 0.0001) stop 3
      if (abs(conjg((1.0, 2.0)) - (1.0, -2.0)) > 0.0001) stop 4
    end subroutine
  end subroutine
end module

module intrinsic_shadow
  implicit none
  integer :: max = 91, conjg = 92
contains
  subroutine check_local_intrinsics
    intrinsic :: max, conjg
    if (max(3, 7) /= 7) stop 5
    if (abs(conjg((2.0, 3.0)) - (2.0, -3.0)) > 0.0001) stop 6
  end subroutine
end module

program intrinsic_bindings
  use intrinsic_provider, only: clock => cpu_time, check_host_intrinsics
  use intrinsic_shadow, only: check_local_intrinsics, host_max => max, host_conjg => conjg
  implicit none
  intrinsic :: max, min, sqrt, conjg, dsqrt
  integer :: abs = 93, intrinsic(2)
  real :: elapsed
  intrinsic(1) = 17
  call check_host_intrinsics
  call check_local_intrinsics
  call check_local_absolute
  call clock(elapsed)
  if (elapsed < 0.0) stop 15
  if (max(2, 5) /= 5 .or. min(2, 5) /= 2) stop 7
  if (sqrt(9.0) < 2.99 .or. sqrt(9.0) > 3.01) stop 8
  if (real(conjg((3.0, 4.0))) < 2.99) stop 9
  if (aimag(conjg((3.0, 4.0))) > -3.99) stop 10
  if (dsqrt(16.0d0) < 3.99d0 .or. dsqrt(16.0d0) > 4.01d0) stop 11
  if (host_max /= 91 .or. host_conjg /= 92 .or. abs /= 93) stop 12
  if (intrinsic(1) /= 17) stop 13
  print '(a)', 'intrinsic binding contracts passed'
contains
  subroutine check_local_absolute
    intrinsic :: abs
    if (abs(-7) /= 7) stop 14
  end subroutine
end program
