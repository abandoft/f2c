include 'dtio_control_types.inc'

program dtio_connection_controls
  use dtio_control_types
  implicit none
  type(payload) :: object
  character(160) :: record
  character(24) :: decimal_mode, round_mode, sign_mode
  namelist /group/ object
  open(25, status='scratch', decimal='comma', round='up', sign='plus', delim='quote')
  object%value = 1.25d0
  write(25, '(DT(1))') object
  inquire(25, decimal=decimal_mode, round=round_mode, sign=sign_mode)
  if (decimal_mode /= 'COMMA' .or. round_mode /= 'UP' .or. sign_mode /= 'PLUS') stop 1
  rewind(25)
  read(25, '(A)') record
  if (record(1:18) /= '  +1,3   1.2  +1,3') stop 2
  write(*, '(A)') record(1:18)
  rewind(25)
  write(25, '(A)') '   0,1'
  rewind(25)
  read(25, '(DT)') object
  write(*, '(Z16.16)') transfer(object%value, 0_8)
  rewind(25)
  object%value = 1.25d0
  write(25, nml=group)
  rewind(25)
  object%value = 0.0d0
  read(25, nml=group)
  if (abs(object%value - 1.3d0) > epsilon(object%value)) stop 3
  close(25)
end program dtio_connection_controls
