module generic_provider
  implicit none
  private
  public :: apply

  interface apply
    module procedure apply_integer, apply_real
  end interface apply
contains
  integer function apply_integer(value) result(answer)
    integer, intent(in) :: value
    answer = value + 1
  end function apply_integer

  real function apply_real(value) result(answer)
    real, intent(in) :: value
    answer = value + 2.0
  end function apply_real
end module generic_provider

program module_generic_check
  use generic_provider, only: apply
  implicit none
  integer :: integer_result
  real :: real_result

  integer_result = apply(3)
  real_result = apply(4.0)
  if (integer_result /= 4) error stop 1
  if (abs(real_result - 6.0) > epsilon(real_result)) error stop 2
  print '(I0,1X,F0.1)', integer_result, real_result
end program module_generic_check
