program equivalence_storage
  implicit none
  integer :: bits
  real :: value
  real :: storage(4), window(2)
  complex :: pair
  real :: components(2)
  character(len=2) :: characters(3), character_view(2)

  equivalence (bits, value)
  equivalence (storage(2), window(1))
  equivalence (pair, components(1))
  equivalence (characters(2), character_view(1))

  bits = 1065353216
  window = (/ 3.5, 4.5 /)
  components = (/ 1.25, -2.5 /)
  character_view = (/ 'ab', 'cd' /)

  write (*, '(5(F0.2,1X))') value, storage(2), storage(3), real(pair), aimag(pair)
  write (*, '(2(A,1X))') characters(2), characters(3)
end program equivalence_storage
