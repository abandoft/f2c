program structure_constructor
  implicit none
  type :: point
    integer :: x
    integer :: y
  end type point
  type(point) :: value

  value = point(3, y=4)
  if (value%x /= 3) stop 1
  if (value%y /= 4) stop 2
end program structure_constructor
