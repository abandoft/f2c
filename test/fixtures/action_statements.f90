program action_statements
  implicit none
  integer :: code

  code = 1
  if (.false.) stop 'unreachable success'
  if (.false.) error stop code
  if (code /= 1) error stop 'invalid action-statement result'
end program action_statements
