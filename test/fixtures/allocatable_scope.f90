module allocatable_scope_support
  implicit none
contains
  function make_values(count) result(values)
    integer, intent(in) :: count
    integer, allocatable :: values(:)
    integer :: index

    allocate(values(0:count - 1))
    do index = 0, count - 1
      values(index) = index + 2
    end do
  end function make_values

  subroutine replace_values(values, count)
    integer, allocatable, intent(inout) :: values(:)
    integer, intent(in) :: count
    integer :: index

    if (allocated(values)) deallocate(values)
    allocate(values(-1:count - 2))
    do index = -1, count - 2
      values(index) = 10 + index
    end do
  end subroutine replace_values
end module allocatable_scope_support

program allocatable_scope
  use allocatable_scope_support, only: make_values, replace_values
  implicit none
  integer, allocatable :: values(:)

  values = make_values(4)
  if (values(0) /= 2 .or. values(3) /= 5) stop 1

  call replace_values(values, 3)
  if (values(-1) /= 9 .or. values(1) /= 11) stop 2
end program allocatable_scope
