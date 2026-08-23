program namelist_zero_length_character
  implicit none

  integer :: status
  integer :: sentinel
  character(len=0) :: empty
  character(256) :: record
  namelist /sample/ empty, sentinel

  empty = ''
  sentinel = 17
  record = "&sample sentinel=99, empty='x' /"
  read(record, nml=sample, iostat=status)

  if (status == 0) stop 1
  if (sentinel /= 17) stop 2
  if (len(empty) /= 0) stop 3
end program namelist_zero_length_character
