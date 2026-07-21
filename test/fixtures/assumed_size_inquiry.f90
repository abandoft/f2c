program assumed_size_inquiry
  implicit none
  integer :: values(3, 3), observed(10)

  values = reshape([1, 2, 3, 4, 5, 6, 7, 8, 9], [3, 3])
  call inspect(values, 3, observed)
  if (any(observed /= [3, 3, 0, -2, 0, -2, 2, 9, 9, 45])) error stop 1

  write (*, '(10(I0,1X))') observed

contains

  subroutine inspect(array, leading, result)
    integer, intent(in) :: leading
    integer, intent(in) :: array(0:leading - 1, -2:*)
    integer, intent(out) :: result(10)
    integer :: dim

    dim = 1
    result(1) = size(array, 1)
    result(2) = size(array, dim)
    result(3:4) = lbound(array)
    result(5) = lbound(array, dim)
    dim = 2
    result(6) = lbound(array, dim)
    dim = 1
    result(7) = ubound(array, dim)
    result(8) = array(2, 0)
    result(9) = size(array(:, -2:0))
    result(10) = sum(array(:, -2:0))
  end subroutine inspect

end program assumed_size_inquiry
