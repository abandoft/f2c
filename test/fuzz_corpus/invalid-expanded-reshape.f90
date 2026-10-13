program invalid_expanded_reshape
  implicit none
  integer, parameter :: shape(2) = [2,-1]
  integer :: matrix(2,2), i
  matrix = reshape([1,2,3,4], shape)
  matrix = reshape([1,2,3,4], [(i,i=2,3)], order=[(i,i=1,1),1])
end program
