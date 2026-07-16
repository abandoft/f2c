program array_constructor_driver
  implicit none
  integer :: values(12)
  character(len=3) :: words(3)
  integer :: i

  call constructor_values(3, 10, values, words)
  write(*, '(12(I0,1X))') values
  write(*, '(4A)') ('|'//words(i), i=1,3), '|'
end program array_constructor_driver
