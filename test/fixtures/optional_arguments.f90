subroutine optional_matrix(output)
  implicit none
  integer, intent(out) :: output(10)
  real :: values(2)

  values = (/ 2.5, -1.0 /)
  call accumulate(1, result=output(1))
  call accumulate(1, scalar=2, result=output(2))
  call accumulate(1, values_arg=values, result=output(3))
  call accumulate(1, label='AZ', result=output(4))
  call accumulate(1, 2, values, 'AZ', output(5))
  call accumulate(1, values_arg=values, label='AZ', result=output(6))
  output(7) = choose(1, third=3)
  output(8) = choose(1, second=2)
  call forward(result=output(9))
  call forward(5, output(10))

contains

  subroutine accumulate(base, scalar, values_arg, label, result)
    integer, intent(in) :: base
    integer, intent(in), optional :: scalar
    real, intent(in), optional :: values_arg(*)
    character(len=*), intent(in), optional :: label
    integer, intent(out) :: result

    result = base
    if (present(scalar)) result = result + scalar
    if (present(values_arg)) result = result + nint(values_arg(1) * 10.0)
    if (present(label)) result = result + len(label) + ichar(label(1:1))
  end subroutine accumulate

  integer function choose(first, second, third)
    integer, intent(in) :: first
    integer, intent(in), optional :: second, third

    choose = first
    if (present(second)) choose = choose + 10 * second
    if (present(third)) choose = choose + 100 * third
  end function choose

  subroutine forward(value, result)
    integer, intent(in) :: value
    integer, intent(out) :: result
    optional :: value

    call accumulate(10, scalar=value, result=result)
  end subroutine forward
end subroutine optional_matrix
