subroutine daxpy(n, alpha, x, incx, y, incy)
  implicit none
  integer, intent(in) :: n, incx, incy
  double precision, intent(in) :: alpha
  double precision, intent(in) :: x(*)
  double precision, intent(inout) :: y(*)
  integer :: i, ix, iy

  ix = 1
  iy = 1
  do i = 1, n
    y(iy) = y(iy) + alpha * x(ix)
    ix = ix + incx
    iy = iy + incy
  end do
end subroutine daxpy
