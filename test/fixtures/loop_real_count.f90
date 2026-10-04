program loop_real_count
  use iso_fortran_env, only: real32, real64, int32, int64
  implicit none
  real(real32), parameter :: first4(7) = [0.0, 1.0, 1.0, -1.0, 0.0, 2.0, -0.3]
  real(real32), parameter :: last4(7) = [1.0, 0.0, -1.0, 1.0, -1.0, 1.0, 0.3]
  real(real32), parameter :: step4(7) = [0.1, -0.1, -0.3, 0.3, -0.1, 0.1, 0.2]
  real(real64), parameter :: first8(7) = [0d0, 1d0, 1d0, -1d0, 0d0, 2d0, -0.3d0]
  real(real64), parameter :: last8(7) = [1d0, 0d0, -1d0, 1d0, -1d0, 1d0, 0.3d0]
  real(real64), parameter :: step8(7) = [0.1d0, -0.1d0, -0.3d0, 0.3d0, -0.1d0, 0.1d0, 0.2d0]
  integer, parameter :: expected4(7) = [11, 11, 7, 7, 11, 0, 4]
  integer, parameter :: expected8(7) = [11, 11, 7, 7, 11, 0, 3]
  real(real32) :: iterator4
  real(real64) :: iterator8, inner, captured
  integer :: test, count4, count8, calls, visits

  do test = 1, 7
    count4 = 0
    do iterator4 = first4(test), last4(test), step4(test)
      count4 = count4 + 1
      if (count4 > 20) stop 1
    end do
    count8 = 0
    do iterator8 = first8(test), last8(test), step8(test)
      count8 = count8 + 1
      if (count8 > 20) stop 2
    end do
    if (count4 /= expected4(test) .or. count8 /= expected8(test)) stop 3
    print '(I1,1X,I2,1X,I2,1X,Z8.8,1X,Z16.16)', test, count4, count8, &
      transfer(iterator4, 0_int32), transfer(iterator8, 0_int64)
  end do

  ! Rounding makes the induction value stationary; the pre-count still terminates.
  count4 = 0
  do iterator4 = 16777216.0, 16777216.0, 1.0
    count4 = count4 + 1
    if (count4 > 2) stop 4
  end do
  if (count4 /= 1) stop 5
  print '(I1,1X,Z8.8)', count4, transfer(iterator4, 0_int32)

  ! Bounds are captured once, and later writes cannot change the established count.
  calls = 0
  captured = 1.0_real64
  count8 = 0
  do iterator8 = control(0.0_real64), control(captured), control(0.1_real64)
    captured = 100.0_real64
    count8 = count8 + 1
    if (count8 > 20) stop 6
  end do
  if (calls /= 3 .or. count8 /= 11) stop 7

  visits = 0
  outer: do iterator8 = 0.0_real64, 1.0_real64, 0.25_real64
    do inner = 0.0_real64, 1.0_real64, 0.5_real64
      visits = visits + 1
      if (visits == 2) cycle outer
      if (visits == 5) exit outer
    end do
  end do outer
  if (visits /= 5 .or. abs(iterator8 - 0.25_real64) > 1d-15) stop 8
  print '(A)', 'legacy real pre-count, captured bounds and named exits passed'
contains
  function control(value) result(result_value)
    real(real64), intent(in) :: value
    real(real64) :: result_value
    calls = calls + 1
    result_value = value
  end function
end program
