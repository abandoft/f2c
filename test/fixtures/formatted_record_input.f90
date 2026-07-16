program formatted_record_input
  implicit none

  character(len=8) :: line, next_line
  integer :: status

  open(unit=41, status='scratch', iostat=status)
  if (status /= 0) error stop

  write(41, '(A)') 'alpha'
  write(41, '(A)') 'bravo'
  write(41, '(A)') 'charlie'
  rewind(41)

  read(41, '(A8,/A8)', iostat=status) line, next_line
  if (status /= 0 .or. line /= 'alpha' .or. next_line /= 'bravo') error stop
  rewind(41)

  read(41, '(A8)', iostat=status) line
  if (status /= 0 .or. line /= 'alpha') error stop
  read(41, '(A8)', iostat=status) line
  if (status /= 0 .or. line /= 'bravo') error stop
  read(41, '(A8)', iostat=status) line
  if (status /= 0 .or. line /= 'charlie') error stop
  read(41, '(A8)', iostat=status) line
  if (status >= 0) error stop

  close(41)
end program formatted_record_input
