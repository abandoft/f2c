module dispatch_types
  implicit none
  type :: base_counter
    integer :: value
  contains
    procedure :: score => base_score
  end type base_counter
  type, extends(base_counter) :: scaled_counter
    integer :: scale
  contains
    procedure :: score => scaled_score
  end type scaled_counter
contains
  subroutine verify_dynamic(object, expected)
    class(base_counter), intent(in) :: object
    integer, intent(in) :: expected
    if (object%score() /= expected) stop 3
  end subroutine verify_dynamic

  integer function base_score(self)
    class(base_counter), intent(in) :: self
    base_score = self%value
  end function base_score

  integer function scaled_score(self)
    class(scaled_counter), intent(in) :: self
    scaled_score = self%value * self%scale
  end function scaled_score
end module dispatch_types

program type_bound_dispatch
  use dispatch_types
  implicit none
  type(base_counter) :: base
  type(scaled_counter) :: child

  base%value = 7
  child%value = 6
  child%scale = 5
  if (base%score() /= 7) stop 1
  if (child%score() /= 30) stop 2
  call verify_dynamic(child, 30)
end program type_bound_dispatch
