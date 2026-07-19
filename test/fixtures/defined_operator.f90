module defined_operator_provider
  implicit none
  private
  public :: box, operator(+), operator(.combine.), operator(.invert.)

  type :: box
    integer :: value
  end type box

  interface operator(+)
    procedure :: add_box
  end interface operator(+)

  interface operator(.combine.)
    procedure :: combine_integer, combine_real
  end interface operator(.combine.)

  interface operator(.invert.)
    procedure :: invert_integer
  end interface operator(.invert.)
contains
  type(box) function add_box(left, right) result(answer)
    type(box), intent(in) :: left, right
    answer%value = left%value + right%value
  end function add_box

  integer function combine_integer(left, right) result(answer)
    integer, intent(in) :: left, right
    answer = left * 10 + right
  end function combine_integer

  real function combine_real(left, right) result(answer)
    real, intent(in) :: left, right
    answer = left * 10.0 + right
  end function combine_real

  integer function invert_integer(value) result(answer)
    integer, intent(in) :: value
    answer = -value
  end function invert_integer
end module defined_operator_provider

program defined_operator_check
  use defined_operator_provider, only: box, operator(+), operator(.combine.), operator(.invert.)
  implicit none
  integer :: integer_result, precedence_result, unary_result
  real :: real_result
  type(box) :: box_left, box_right, box_result

  integer_result = 2 .combine. 3
  real_result = 1.0 .combine. 2.5
  precedence_result = 1 + 2 .combine. 3 * 4
  unary_result = .invert. 2 ** 2
  box_left%value = 2
  box_right%value = 3
  box_result = box_left + box_right
  if (integer_result /= 23) error stop 1
  if (abs(real_result - 12.5) > epsilon(real_result)) error stop 2
  if (precedence_result /= 42) error stop 3
  if (unary_result /= 4) error stop 4
  if (box_result%value /= 5) error stop 5
  print '(I0,1X,F0.1,1X,I0,1X,I0,1X,I0)', integer_result, real_result, precedence_result, &
    unary_result, box_result%value
end program defined_operator_check
