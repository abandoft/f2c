module module_allocatable_storage
  implicit none
  integer, allocatable :: values(:)
contains
  subroutine initialize_values()
    allocate(values(3))
    values(1) = 2
    values(2) = 3
    values(3) = 5
  end subroutine initialize_values

  integer function total_values()
    total_values = values(1) + values(2) + values(3)
  end function total_values
end module module_allocatable_storage

program module_allocatable_user
  use module_allocatable_storage, only: initialize_values, total_values
  implicit none
  call initialize_values()
  if (total_values() /= 10) stop 1
end program module_allocatable_user
