subroutine reverse_cube(cube, n)
  implicit none
  integer, intent(in) :: n
  real, intent(inout) :: cube(n,n,n)
  cube(1:n,1:n,1:n) = cube(n:1:-1,n:1:-1,n:1:-1)
end subroutine reverse_cube
