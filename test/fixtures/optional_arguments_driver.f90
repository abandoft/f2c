program optional_arguments_driver
  implicit none
  integer :: output(10)

  call optional_matrix(output)
  write (*, '(10(I0,1X))') output
end program optional_arguments_driver
