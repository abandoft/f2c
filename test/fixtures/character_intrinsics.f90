program character_intrinsics
  implicit none
  character(len=6) :: text
  character(len=4) :: values(2), adjusted(2)
  integer :: positions(2), lengths(2)
  integer(8) :: wide

  text = '  AB  '
  if (adjustl(text) /= 'AB    ') error stop 1
  if (adjustr(text) /= '    AB') error stop 2
  if (trim(text) /= '  AB') error stop 3
  if (repeat('xy', 3) /= 'xyxyxy') error stop 4
  if (char(65) /= 'A' .or. achar(66) /= 'B') error stop 5
  if (ichar('X') /= 88 .or. iachar(achar(65)) /= 65) error stop 6
  if (ichar(char(0)) /= 0) error stop 7
  if (len(text) /= 6 .or. len_trim(text) /= 4) error stop 8
  if (index('FORTRAN', 'R', back=.true.) /= 5) error stop 9
  if (index('ABC', '') /= 1 .or. index('ABC', '', back=.true.) /= 4) error stop 10
  if (scan('FORTRAN', 'TR') /= 3 .or. scan('FORTRAN', 'TR', back=.true.) /= 5) error stop 11
  if (verify('ABBA', 'A') /= 2 .or. verify('ABBA', 'A', back=.true.) /= 3) error stop 12

  values = [' A  ', '  B ']
  adjusted = adjustl(values)
  positions = index(values, 'B')
  lengths = len_trim(values)
  if (any(adjusted /= ['A   ', 'B   '])) error stop 13
  if (any(positions /= [0, 3])) error stop 14
  if (any(lengths /= [2, 3])) error stop 15
  if (len(values) /= 4) error stop 16
  wide = index('ABCABC', 'ABC', back=.true., kind=8)
  if (wide /= 4_8) error stop 17

  print '(A,1X,A,1X,I0)', 'CHARACTER', trim(adjustl(text)), wide
end program character_intrinsics
