program statement_function_test
  real :: x, y
  x = 2.0
  call statement_function_case(x, y)
  if (abs(y - 11.0) > 1.0e-6) stop 1
end program statement_function_test

subroutine statement_function_case(x, y)
  real :: x, y, square, affine
  square(t) = t * t
  affine(t) = square(t) + x
  y = affine(x + 1.0)
end subroutine statement_function_case
