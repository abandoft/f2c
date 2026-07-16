program explicit_interface_driver
  implicit none
  integer :: results(9)

  call explicit_interface_matrix(results)
  write(*, '(8(i0,1x),i0)') results
end program explicit_interface_driver
