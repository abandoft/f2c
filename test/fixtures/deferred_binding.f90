module deferred_types
  implicit none
  type, abstract :: abstract_counter
  contains
    procedure(counter_value), deferred :: value
  end type abstract_counter

  abstract interface
    function counter_value(self) result(result_value)
      import abstract_counter
      class(abstract_counter), intent(in) :: self
      integer :: result_value
    end function counter_value
  end interface

  type, extends(abstract_counter) :: concrete_counter
    integer :: stored
  contains
    procedure :: value => concrete_value
  end type concrete_counter
contains
  integer function concrete_value(self)
    class(concrete_counter), intent(in) :: self
    concrete_value = self%stored
  end function concrete_value

  subroutine check_counter(self)
    class(abstract_counter), intent(in) :: self
    if (self%value() /= 41) stop 1
  end subroutine check_counter
end module deferred_types

program deferred_binding
  use deferred_types
  implicit none
  type(concrete_counter) :: counter
  counter%stored = 41
  call check_counter(counter)
end program deferred_binding
