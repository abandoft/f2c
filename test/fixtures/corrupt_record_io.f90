program corrupt_record_io
  implicit none
  integer, parameter :: unit_number = 61
  integer :: status, value
  integer(kind=1) :: marker
  character(64) :: message

  open(unit=unit_number, file='f2c-corrupt-record.tmp', status='replace', &
       access='sequential', form='unformatted', action='write', &
       iostat=status, iomsg=message)
  if (status /= 0) error stop
  write(unit_number, iostat=status, iomsg=message) 1234
  if (status /= 0) error stop
  close(unit_number, iostat=status, iomsg=message)
  if (status /= 0) error stop

  open(unit=unit_number, file='f2c-corrupt-record.tmp', status='old', &
       access='direct', form='unformatted', recl=1, action='write', &
       iostat=status, iomsg=message)
  if (status /= 0) error stop
  marker = 0
  write(unit_number, rec=1, iostat=status, iomsg=message) marker
  if (status /= 0) error stop
  close(unit_number, iostat=status, iomsg=message)
  if (status /= 0) error stop

  open(unit=unit_number, file='f2c-corrupt-record.tmp', status='old', &
       access='sequential', form='unformatted', action='read', &
       iostat=status, iomsg=message)
  if (status /= 0) error stop
  value = 0
  read(unit_number, iostat=status, iomsg=message) value
  if (status <= 0) error stop
  close(unit_number, status='delete', iostat=status, iomsg=message)
  if (status /= 0) error stop
end program corrupt_record_io
