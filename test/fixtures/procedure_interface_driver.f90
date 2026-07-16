program procedure_interface_driver
  implicit none
  integer :: results(10)

  call procedure_interface_matrix(results)
  print '(10(I0,1X))', results
end program procedure_interface_driver
