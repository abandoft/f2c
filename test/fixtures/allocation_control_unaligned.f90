module allocation_unaligned_control_cases
  implicit none
contains
  pure function slot() result(value)
    integer, allocatable :: value
    allocate(value)
    value = 2
  end function
end module
program allocation_control_unaligned
  use allocation_unaligned_control_cases
  implicit none
  integer(kind=1) :: bytes(14)
  integer, volatile :: statuses(3)
  integer, allocatable :: value
  equivalence(bytes(2), statuses(1))
  bytes = 42_1
  statuses = [99,17,99]
  allocate(value, source=statuses(2), stat=statuses(slot()))
  if (value /= 17 .or. any(statuses /= [99,0,99])) stop 1
  if (bytes(1) /= 42_1 .or. bytes(14) /= 42_1) stop 2
  deallocate(value, stat=statuses(slot()))
  if (allocated(value) .or. any(statuses /= [99,0,99])) stop 3
  deallocate(value, stat=statuses(slot()))
  if (allocated(value) .or. statuses(2) <= 0) stop 4
  if (statuses(1) /= 99 .or. statuses(3) /= 99) stop 5
  if (bytes(1) /= 42_1 .or. bytes(14) /= 42_1) stop 6
  print '(A)', 'allocation unaligned controls passed'
end program
