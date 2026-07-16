program internal_file_array
  implicit none
  character(len=12) :: records(2)
  integer :: first
  integer :: second

  first = 17
  second = -29
  write(records, '(I5/I5)') first, second
  first = 0
  second = 0
  read(records, '(I5/I5)') first, second
  if (first /= 17) stop 1
  if (second /= -29) stop 2
end program internal_file_array
