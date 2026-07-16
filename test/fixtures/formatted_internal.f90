program formatted_internal
  implicit none
  character(len=80) :: record
  character(len=2) :: word
  integer :: count
  integer :: status
  real :: scale
  logical :: enabled
  integer :: first
  integer :: second
  integer :: third

  count = 42
  scale = 1.25
  enabled = .true.
  word = 'OK'
  write(record, 100) count, scale, enabled, word

  count = 0
  scale = 0.0
  enabled = .false.
  word = 'NO'
  read(record, 100) count, scale, enabled, word

  if (count /= 42 .or. abs(scale - 1.25) > 1.0e-6 .or. .not. enabled .or. &
      word /= 'OK') stop 1
  write(record, '(A)', iostat=status) count
  if (status == 0) stop 2
  first = 2
  second = 3
  third = 5
  write(record, '(*(I0,:,","))') first, second, third
  if (record(1:5) /= '2,3,5') stop 3
  write(record, '(*I0)', iostat=status) first
  if (status == 0) stop 4
  scale = 1.1920929e-7
  write(record, '(1P,E9.1)') scale
  if (record(1:9) /= '  1.2E-07') stop 5
  scale = 0.0
  read(record, '(1P,E9.1)') scale
  if (abs(scale - 1.2e-7) > 1.0e-12) stop 6
100 format(i5, 1x, f8.2, 1x, l1, 1x, a2)
end program formatted_internal
