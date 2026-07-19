program internal_file_semantics
  implicit none
  character(len=3) :: short_record
  character(len=8) :: output_record
  character(len=4) :: records(3)
  integer :: first
  integer :: second
  integer :: status

  short_record = '12 '
  first = 0
  read(short_record, '(I5)', iostat=status) first
  if (status /= 0 .or. first /= 12) stop 1

  output_record = 'existing'
  write(output_record, '(A2,2X,I2)', iostat=status) 'OK', 7
  if (status /= 0 .or. output_record /= 'OK   7  ') stop 2

  records = 'keep'
  write(records, '(I2/I2)', iostat=status) 11, 22
  if (status /= 0) stop 3
  if (records(1) /= '11  ' .or. records(2) /= '22  ' .or. records(3) /= 'keep') stop 4

  first = 0
  second = 0
  read(records, '(I2/I2)', iostat=status) first, second
  if (status /= 0 .or. first /= 11 .or. second /= 22) stop 5

  write(short_record, '(A4)', iostat=status) 'wide'
  if (status == 0) stop 6

end program internal_file_semantics
