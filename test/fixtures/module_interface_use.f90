module math_interfaces
  implicit none
  interface
    integer function increment(value)
      implicit none
      integer, intent(in) :: value
    end function increment
  end interface
end module math_interfaces

integer function increment(value)
  implicit none
  integer, intent(in) :: value
  increment = value + 1
end function increment

program module_interface_use
  use math_interfaces, only: local_increment => increment
  implicit none
  if (local_increment(4) /= 5) error stop 1
end program module_interface_use
