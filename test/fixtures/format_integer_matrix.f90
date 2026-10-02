program format_integer_matrix
  implicit none
  integer(kind=1) :: byte
  integer(kind=2) :: half
  integer(kind=4) :: word
  integer(kind=8) :: wide
  character(600) :: record
  character(32) :: dynamic_format
  integer :: status
  byte = -1
  half = -1
  word = -1
  wide = -1
  write(*, '(Z2.2,1X,Z4.4,1X,Z8.8,1X,Z16.16)') byte, half, word, wide
  write(*, '(B8.8,1X,O6.6,1X,O11.11,1X,O22.22)') byte, half, word, wide
  write(*, '("|",I4.0,"|",I0.0,"|",SP,I4.0,"|",I0,"|")') 0, 0, 0, 0
  write(*, '("|",I0.10,"|",SP,I0.10,"|")') -42, 42
  write(record, '(I600.500)') 42
  if (record(99:100) /= '  ' .or. record(599:600) /= '42') stop 1
  if (record(101:598) /= repeat('0', 498)) stop 2
  write(*, '(A)') record
  dynamic_format = '(I600.500)'
  write(record, dynamic_format) -42
  if (record(99:99) /= ' ' .or. record(100:100) /= '-') stop 3
  if (record(599:600) /= '42') stop 4
  write(*, '(A)') record
  write(*, '("|",I3.2,"|")') 4242
  write(*, '("star |",3X,I2.1,"|",T18,I2.1,"|")') 4242, 4242
  write(*, '("real |",3X,F4.1,"|",T20,F4.1,"|")') 4242.0, 4242.0
  dynamic_format = '(I5.1000)'
  write(record, dynamic_format, iostat=status) 42
  if (status == 0) stop 5
end program format_integer_matrix
