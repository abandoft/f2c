subroutine adjust(value, scale, values, tag)
  implicit none
  integer, intent(inout) :: value
  real, intent(in), optional :: scale
  real, intent(inout) :: values(3)
  character(*), intent(in) :: tag
  integer :: index

  if (len(tag) == 4 .and. tag(1:2) == 'AB') value = value + 1
  if (present(scale)) then
    value = value + int(scale)
  else
    value = value + 7
  end if
  do index = 1, 3
    values(index) = values(index) + real(value)
  end do
end subroutine adjust

real function score_impl(values, offset, bias) result(total)
  implicit none
  real, intent(in) :: values(3)
  real, intent(in), optional :: offset
  real, intent(in) :: bias

  total = sum(values) + bias
  if (present(offset)) total = total + offset
end function score_impl

real function classify_integer(value)
  implicit none
  integer, intent(in) :: value

  classify_integer = real(value) + 100.0
end function classify_integer

real function classify_real(value)
  implicit none
  real, intent(in) :: value

  classify_real = value + 200.0
end function classify_real
