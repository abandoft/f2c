subroutine power_wide(base, exponent, value)
  implicit none
  integer(kind=8), intent(in) :: base, exponent
  integer(kind=8), intent(out) :: value
  value = base ** exponent
end subroutine

subroutine power_narrow(base, exponent, value)
  implicit none
  integer(kind=1), intent(in) :: base, exponent
  integer(kind=1), intent(out) :: value
  value = base ** exponent
end subroutine

subroutine logical_truth(left, right, scalar_equal, array_equal, different_count)
  implicit none
  logical(kind=8), intent(in) :: left(3), right(3)
  logical(kind=8), intent(out) :: scalar_equal, array_equal
  integer, intent(out) :: different_count
  scalar_equal = left(1) .eqv. right(1)
  array_equal = all(left .eqv. right)
  different_count = count(left .neqv. right)
end subroutine
