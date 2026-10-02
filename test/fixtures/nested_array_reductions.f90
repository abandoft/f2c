program nested_array_reductions
  implicit none
  real(8) :: values(3), large
  integer :: matrix(2,3)
  values = [3.5d0,3.5d0,-4.25d0]
  large = 1.0d0
  if (any(abs(values-[3.5d0,3.5d0,-4.25d0]) > epsilon(large))) stop 1
  if (.not. all(abs(values-[0.0d0,0.0d0,0.0d0]) > epsilon(large))) stop 2
  if (count(abs(values-[3.5d0,0.0d0,-4.25d0]) > epsilon(large)) /= 1) stop 3
  if (sum(abs(values-[0.5d0,0.5d0,-0.25d0])) /= 10.0d0) stop 4
  matrix = reshape([1,2,3,4,5,6],[2,3])
  if (any(abs(matrix-reshape([1,2,3,4,5,6],[2,3])) > 0)) stop 5
  if (.not. all(abs(matrix-reshape([0,0,0,0,0,0],[2,3])) > 0)) stop 6
  if (any(shape(matrix) /= [2,3])) stop 7
  if (.not. any(shape(matrix) /= [2,4])) stop 8
  write(*,'(a)') 'nested array reductions passed'
end program
