program nonadvancing_io
  implicit none
  integer :: status
  integer :: transferred
  character(len=3) :: first
  character(len=4) :: second
  character(len=32) :: message

  open(unit=31, file='f2c_nonadvancing_io.tmp', status='replace', iostat=status)
  if (status /= 0) stop 1
  write(31, '(a)', advance='no', iostat=status) 'abc'
  if (status /= 0) stop 2
  write(31, '(a)', iostat=status) 'def'
  if (status /= 0) stop 3
  rewind(31)

  transferred = -1
  read(31, '(a3)', advance='no', size=transferred, iostat=status) first
  if (status /= 0 .or. transferred /= 3 .or. first /= 'abc') stop 4

  transferred = -1
  message = ''
  read(31, '(a4)', advance='no', size=transferred, eor=100, iostat=status, iomsg=message) second
  stop 5
100 continue
  if (status /= -2) stop 6
  if (transferred /= 3) stop 7
  if (second /= 'def ') stop 8
  if (len_trim(message) == 0) stop 9
  close(31)
end program nonadvancing_io
