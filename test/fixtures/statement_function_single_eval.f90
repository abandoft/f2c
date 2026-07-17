program statement_function_single_eval_test
  integer :: counter, result
  counter = 0
  call apply_once(counter, result)
  if (counter /= 1) stop 1
  if (result /= 2) stop 2
end program statement_function_single_eval_test

integer function advance(counter)
  integer :: counter
  counter = counter + 1
  advance = counter
end function advance

subroutine apply_once(counter, result)
  integer :: counter, result, twice, advance, t
  external advance
  twice(t) = t + t
  result = twice(advance(counter))
end subroutine apply_once
