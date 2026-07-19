module explicit_generic_provider
  implicit none

  interface offset
    integer function offset_integer(value)
      integer, intent(in) :: value
    end function offset_integer

    real function offset_real(value)
      real, intent(in) :: value
    end function offset_real
  end interface offset
end module explicit_generic_provider

integer function offset_integer(value)
  implicit none
  integer, intent(in) :: value
  offset_integer = value + 2
end function offset_integer

real function offset_real(value)
  implicit none
  real, intent(in) :: value
  offset_real = value + 3.0
end function offset_real

program module_explicit_generic_check
  use explicit_generic_provider, only: offset
  implicit none
  integer :: integer_result
  real :: real_result

  integer_result = offset(3)
  real_result = offset(4.0)
  if (integer_result /= 5) error stop 1
  if (abs(real_result - 7.0) > epsilon(real_result)) error stop 2
  print '(I0,1X,F0.1)', integer_result, real_result
end program module_explicit_generic_check
