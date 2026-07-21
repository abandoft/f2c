module array_section_bound_state
  implicit none
  integer :: lower_calls, upper_calls, stride_calls, scalar_calls, vector_calls

contains

  integer function lower_bound(value_arg) result(bound)
    integer, intent(in) :: value_arg
    lower_calls = lower_calls + 1
    bound = value_arg
  end function lower_bound

  integer function upper_bound(value_arg) result(bound)
    integer, intent(in) :: value_arg
    upper_calls = upper_calls + 1
    bound = value_arg
  end function upper_bound

  integer function section_stride() result(step)
    stride_calls = stride_calls + 1
    step = 1
  end function section_stride

  integer function scalar_value() result(value_result)
    scalar_calls = scalar_calls + 1
    value_result = 9
  end function scalar_value

  integer function vector_index(value_arg) result(index_result)
    integer, intent(in) :: value_arg
    vector_calls = vector_calls + 1
    index_result = value_arg
  end function vector_index

end module array_section_bound_state

program array_section_bounds_once
  use array_section_bound_state
  implicit none
  integer :: target(6), value(6)

  target = 0
  value = [1, 2, 3, 4, 5, 6]
  lower_calls = 0
  upper_calls = 0
  stride_calls = 0
  scalar_calls = 0
  vector_calls = 0

  target(lower_bound(2):upper_bound(4):section_stride()) = &
      value(lower_bound(1):upper_bound(3):section_stride())
  target(lower_bound(1):upper_bound(3)) = scalar_value()
  target([vector_index(5), vector_index(6)]) = &
      value([vector_index(5), vector_index(6)])

  if (any(target /= [9, 9, 9, 3, 5, 6])) error stop 1
  if (lower_calls /= 3 .or. upper_calls /= 3 .or. &
      stride_calls /= 2 .or. scalar_calls /= 1 .or. vector_calls /= 4) error stop 2

  write (*, '(11(I0,1X))') target, lower_calls, upper_calls, stride_calls, scalar_calls, &
      vector_calls
end program array_section_bounds_once
