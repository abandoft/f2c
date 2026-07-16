program deferred_character_driver
  implicit none
  integer :: output(56)

  call deferred_character_matrix(output)
  write (*, '(56(I0,1X))') output
end program deferred_character_driver
