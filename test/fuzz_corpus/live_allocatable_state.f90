0module allocatable_state_seed
contains
  function fill(values) result(copy)
    integer, allocatable, volatile, intent(inout) :: values(:)
    integer, allocatable :: copy(:)
    if (allocated(values)) deallocate(values)
    allocate(values(-1:1))
    values = [4,5,6]
    copy = values + 10
  end function
end module
program allocatable_state_main
  use allocatable_state_seed
  integer, allocatable :: values(:), copy(:), moved(:)
  copy = fill(values)
  if (values(0) /= 5 .or. copy(1) /= 14) stop 1
  call move_alloc(values,moved)
  if (allocated(values) .or. sum(moved) /= 15) stop 2
  deallocate(copy,moved)
end program
