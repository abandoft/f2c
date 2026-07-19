module defined_assignment_provider
  implicit none
  private
  public :: box, assignment(=)

  type :: box
    integer :: value
  end type box

  interface assignment(=)
    procedure :: assign_integer
  end interface assignment(=)
contains
  elemental subroutine assign_integer(target, source)
    type(box), intent(out) :: target
    integer, intent(in) :: source
    target%value = source * 2
  end subroutine assign_integer
end module defined_assignment_provider

program defined_assignment_check
  use defined_assignment_provider, only: box, assignment(=)
  implicit none
  type(box) :: scalar, values(3)

  scalar = 7
  values = [1, 2, 3]
  if (scalar%value /= 14) error stop 1
  if (values(1)%value /= 2 .or. values(2)%value /= 4 .or. values(3)%value /= 6) error stop 2
  print '(I0,1X,I0,1X,I0,1X,I0)', scalar%value, values(1)%value, values(2)%value, values(3)%value
end program defined_assignment_check
