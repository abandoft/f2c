module geometry
  implicit none
  integer, parameter :: answer = 42
  integer :: shared_value
  type :: point
    integer :: x
    integer :: y
  end type point
end module geometry

program module_use
  use geometry, only: answer, shared_value, point
  implicit none
  type(point) :: value

  value%x = answer
  value%y = 8
  shared_value = value%x + value%y
  if (shared_value /= 50) stop 1
end program module_use
