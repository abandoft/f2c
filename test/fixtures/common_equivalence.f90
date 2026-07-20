program common_equivalence
  implicit none
  integer :: shared(2), extension(2)
  integer :: blank_shared(2), blank_extension(2)
  common /state/ shared
  common blank_shared
  equivalence (shared(2), extension(1))
  equivalence (blank_shared(2), blank_extension(1))

  call populate_common_equivalence()
  write (*, '(4(I0,1X))') shared(1), shared(2), extension(1), extension(2)
  write (*, '(4(I0,1X))') blank_shared(1), blank_shared(2), &
      blank_extension(1), blank_extension(2)
end program common_equivalence

subroutine populate_common_equivalence()
  implicit none
  integer :: values(3)
  integer :: blank_values(3)
  common /state/ values
  common blank_values

  values = (/ 11, 22, 33 /)
  blank_values = (/ 44, 55, 66 /)
end subroutine populate_common_equivalence
