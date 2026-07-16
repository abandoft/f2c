program derived_type
  implicit none
  type :: point
    integer :: x
    integer :: y
    real :: weight
  end type point
  type(point) :: value

  value%x = 3
  value%y = 4
  value%weight = 2.5
  if (value%x + value%y /= 7) stop 1
  if (abs(value%weight - 2.5) > 1.0e-6) stop 2
end program derived_type
