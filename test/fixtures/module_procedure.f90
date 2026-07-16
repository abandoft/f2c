module state_module
  implicit none
  integer :: shared_value
contains
  subroutine set_shared(value)
    implicit none
    integer, intent(in) :: value
    shared_value = value
  end subroutine set_shared
end module state_module

program module_procedure
  use state_module, only: shared_value, set_shared
  implicit none
  call set_shared(37)
  if (shared_value /= 37) stop 1
end program module_procedure
