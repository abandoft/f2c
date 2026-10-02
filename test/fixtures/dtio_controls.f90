! J3/18-007r1 12.5.2 requires temporary connection-mode changes to affect
! nested child transfers. gfortran 16.1 ignores these parent overrides in DT
! children; this fixture uses standard assertions, not native byte comparison.
include 'dtio_control_types.inc'

program dtio_controls
  use dtio_control_types
  implicit none
  type(payload) :: object
  character(160) :: record
  character(32) :: records(2)
  character(24) :: decimal_mode, round_mode, sign_mode, dynamic_mode
  character(64) :: dynamic_format
  integer :: status
  namelist /group/ object
  object%value = 1.25d0
  write(record, '(DC,RU,SP,DT(1))') object
  if (record(1:18) /= '  +1,3   1.2  +1,3') stop 6
  write(*, '(A)') record(1:18)
  write(record, '(DC,RU,SP,3X,DT(1),T25,A)') object, 'tail'
  if (record(4:21) /= '  +1,3   1.2  +1,3' .or. record(25:28) /= 'tail') stop 19
  records = ''
  write(records, '(DC,RU,SP,DT(2),A)') object, 'tail'
  record = records(1)
  if (record(1:6) /= '  +1,3') stop 20
  record = records(2)
  if (record(1:16) /= '   1.2  +1,3tail') stop 21
  write(record, '(DT(1))', decimal='comma', round='up', sign='plus') object
  if (record(1:18) /= '  +1,3   1.2  +1,3') stop 7
  write(*, '(A)') record(1:18)
  write(record, *, decimal='comma', round='up', sign='plus') object
  if (trim(adjustl(record)) /= '+1,3') stop 8
  write(record, nml=group, decimal='comma', round='up', sign='plus', delim='quote')
  object%value = 0.0d0
  read(record, nml=group, decimal='comma', round='up')
  if (abs(object%value - 1.3d0) > epsilon(object%value)) stop 9
  record = '  0,1'
  read(record, '(DT)', decimal='comma', round='up') object
  write(*, '(Z16.16)') transfer(object%value, 0_8)
  read(record, '(DC,RU,DT)') object
  if (transfer(object%value, 0_8) /= 4591870180066957722_8) stop 18
  record = '0,1'
  read(record, *, decimal='comma', round='up') object
  write(*, '(Z16.16)') transfer(object%value, 0_8)

  open(25, status='scratch', decimal='point', round='nearest', sign='suppress')
  object%value = 1.25d0
  write(25, '(DT(1))', decimal='comma', round='up', sign='plus') object
  inquire(25, decimal=decimal_mode, round=round_mode, sign=sign_mode)
  if (decimal_mode /= 'POINT' .or. round_mode /= 'NEAREST' .or. sign_mode /= 'SUPPRESS') stop 10
  dynamic_mode = 'invalid'
  write(25, *, decimal='comma', round=dynamic_mode, sign='plus', iostat=status, err=100) object
  stop 11
100 continue
  if (status == 0) stop 12
  inquire(25, decimal=decimal_mode, round=round_mode, sign=sign_mode)
  if (decimal_mode /= 'POINT' .or. round_mode /= 'NEAREST' .or. sign_mode /= 'SUPPRESS') stop 13
  rewind(25)
  read(25, '(A)') record
  if (record(1:18) /= '  +1,3   1.2  +1,3') stop 14
  close(25)
  open(26, status='scratch', decimal='comma', round='up', sign='plus', delim='quote')
  object%value = 1.25d0
  write(26, nml=group)
  rewind(26)
  object%value = 0.0d0
  read(26, nml=group)
  if (abs(object%value - 1.3d0) > epsilon(object%value)) stop 17
  close(26)
  object%value = 1.25d0
  open(27, status='scratch', decimal='comma', round='up', sign='plus')
  write(27, '(SP,SS,DT(4),S,DT(3),SP,DT(5))') object, object, object
  inquire(27, sign=sign_mode)
  if (sign_mode /= 'PLUS') stop 24
  rewind(27)
  read(27, '(A)') record
  if (record(1:18) /= '   1,3   1,3  +1,3') stop 25
  rewind(27)
  dynamic_format = '(SP,SS,DT(4),S,DT(3),SP,DT(5))'
  write(27, dynamic_format) object, object, object
  rewind(27)
  read(27, '(A)') record
  if (record(1:18) /= '   1,3   1,3  +1,3') stop 26
  close(27)
  object%value = 1.25d0
  write(*, '(DT(1))', decimal='comma', round='up', sign='plus') object
  write(*, '(DC,RU,SP,DT(1))') object
end program dtio_controls
