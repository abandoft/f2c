program loop_real_storage
  use iso_fortran_env, only: real64, int32
  implicit none
  real(real64), volatile :: observed
  real(real64), target :: target
  real(real64), pointer :: pointer
  real(real64), allocatable :: allocated
  integer(int32), volatile :: words(4)
  real(real64), volatile :: unaligned
  integer :: trips
  equivalence (words(2), unaligned)

  trips = 0
  do observed = 0d0, 1d0, 0.1d0
    trips = trips + 1
  end do
  if (trips /= 11 .or. abs(observed - 1.1d0) > 1d-14) stop 1
  trips = 0
  do unaligned = 0d0, 1d0, 0.1d0
    trips = trips + 1
  end do
  if (trips /= 11 .or. abs(unaligned - 1.1d0) > 1d-14) stop 2
  pointer => target
  trips = 0
  do pointer = 1d0, 0d0, -0.1d0
    trips = trips + 1
  end do
  if (trips /= 11 .or. abs(target + 0.1d0) > 1d-14) stop 3
  allocate(allocated)
  trips = 0
  do allocated = 0d0, 1d0, 0.1d0
    trips = trips + 1
  end do
  if (trips /= 11 .or. abs(allocated - 1.1d0) > 1d-14) stop 4
  call dummy_loop(pointer)
  if (abs(target - 1.1d0) > 1d-14) stop 5
  deallocate(allocated)
  print '(A)', 'legacy real qualified, associated and unaligned loop storage passed'
contains
  subroutine dummy_loop(iterator)
    real(real64), intent(inout) :: iterator
    integer :: count
    count = 0
    do iterator = 0d0, 1d0, 0.1d0
      count = count + 1
    end do
    if (count /= 11) stop 6
  end subroutine
end program
