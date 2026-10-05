module result_type_scope
  use iso_fortran_env, only: real64, int64
  implicit none
  integer, parameter :: max = real64
contains
  real(real64) function host_kind()
    host_kind = 2.0_real64
  end function
  real(max) function shadowed_intrinsic()
    shadowed_intrinsic = 3.0_real64
  end function
  function local_parameter() result(answer)
    integer, parameter :: local_kind = 8
    real(local_kind) :: answer
    answer = 4.0_local_kind
  end function
  real(selected_kind) function local_use()
    use iso_fortran_env, only: selected_kind => real64
    local_use = 5.0_selected_kind
  end function
  function dummy_kind(value) result(answer)
    real(real64), intent(in) :: value
    real(kind(value)) :: answer
    answer = value
  end function
  integer(int64) function integer_kind()
    integer_kind = 4294967297_int64
  end function
end module

real(selected_kind) function scoped_external()
  use iso_fortran_env, only: selected_kind => real64
  implicit none
  scoped_external = 6.0_selected_kind
end function

program check_result_type_scope
  use result_type_scope
  implicit none
  interface
    real(selected_kind) function scoped_external()
      use iso_fortran_env, only: selected_kind => real64
    end function
  end interface
  if (kind(host_kind()) /= real64 .or. kind(shadowed_intrinsic()) /= real64) stop 1
  if (kind(local_parameter()) /= real64 .or. kind(local_use()) /= real64) stop 2
  if (kind(dummy_kind(7.0_real64)) /= real64 .or. kind(scoped_external()) /= real64) stop 3
  if (abs(host_kind() - 2.0_real64) > tiny(1.0_real64)) stop 4
  if (abs(shadowed_intrinsic() - 3.0_real64) > tiny(1.0_real64)) stop 5
  if (abs(local_parameter() - 4.0_real64) > tiny(1.0_real64)) stop 6
  if (abs(local_use() - 5.0_real64) > tiny(1.0_real64)) stop 7
  if (abs(dummy_kind(7.0_real64) - 7.0_real64) > tiny(1.0_real64)) stop 8
  if (abs(scoped_external() - 6.0_real64) > tiny(1.0_real64)) stop 9
  if (kind(integer_kind()) /= int64 .or. integer_kind() /= 4294967297_int64) stop 10
  print '(A)', 'scoped function-header type selectors passed'
end program
