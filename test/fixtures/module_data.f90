module module_state
  implicit none
  integer :: module_values(2)
  double precision :: module_scale
  complex :: module_point
  character(len=3) :: module_names(2)
  data module_values / 23, 29 /, module_scale / 3.25d0 /
  data module_point / (-1.5, 0.75) /, module_names / 'm', 'two' /
end module module_state

program module_data
  use module_state
  implicit none
  write (*, '(2(I0,1X),F6.2,1X,2(F6.2,1X),A,1X,A)') &
      module_values, module_scale, module_point, module_names(1), module_names(2)
end program module_data
