0
module operations
  implicit none
contains
  subroutine invoke(value)
    integer, intent(in) :: value
    procedure(first), pointer :: selected
    integer :: result
    selected => second
    result = selected(value)
  end subroutine
  integer function first(value)
    integer :: value
    first = value
  end function
  integer function second(value)
    integer :: value
    second = value + 1
  end function
end module
program indirect_interface
  use operations
  implicit none
  call invoke(17)
end program
