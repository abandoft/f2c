subroutine implicit_values(output)
  implicit double precision (a-h, o-z), integer (i-n)
  integer :: output(2)
  alpha = 7.75d0
  index = -3
  output(1) = int(alpha)
  output(2) = index
end subroutine implicit_values

function index_value() result(number)
  implicit integer (i-n)
  number = 11
end function index_value

subroutine implicit_character(cvalue, output)
  implicit character(len=4) (c)
  character(len=4) :: output
  output = cvalue
end subroutine implicit_character
