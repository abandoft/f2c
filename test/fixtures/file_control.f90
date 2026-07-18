program file_control
  implicit none
  integer :: status, value, number
  logical :: exist, opened, named
  character(64) :: name, message
  character(8) :: dynamic_status
  character(16) :: access, sequential, direct, form, formatted, unformatted
  character(16) :: blank, position, action, readable, writable, readwrite, delim, pad
  character(20), parameter :: path = 'f2c-file-control.tmp'

  inquire(file=path, exist=exist, opened=opened, iostat=status, iomsg=message)
  if (status /= 0 .or. exist .or. opened) error stop

  open(unit=21, file=path, status='replace', access='sequential', &
       action='readwrite', form='formatted', blank='null', position='rewind', &
       delim='quote', pad='yes', iostat=status, iomsg=message)
  if (status /= 0) error stop

  write(21, *) 10
  write(21, *) 20
  write(21, *) 30
  backspace(unit=21, iostat=status, iomsg=message)
  if (status /= 0) error stop
  read(21, *, iostat=status, iomsg=message) value
  if (status /= 0 .or. value /= 30) error stop

  rewind(unit=21, iostat=status, iomsg=message)
  if (status /= 0) error stop
  read(21, *, iostat=status, iomsg=message) value
  if (status /= 0 .or. value /= 10) error stop
  endfile(unit=21, iostat=status, iomsg=message)
  if (status /= 0) error stop
  rewind 21
  read(21, *, iostat=status) value
  if (status /= 0 .or. value /= 10) error stop
  read(21, *, iostat=status, iomsg=message) value
  if (status >= 0) error stop

  inquire(unit=21, exist=exist, opened=opened, number=number, named=named, &
          name=name, access=access, sequential=sequential, direct=direct, &
          form=form, formatted=formatted, unformatted=unformatted, blank=blank, &
          position=position, action=action, &
          read=readable, write=writable, readwrite=readwrite, delim=delim, pad=pad, &
          iostat=status, iomsg=message)
  if (status /= 0 .or. .not. exist .or. .not. opened .or. number /= 21) error stop
  if (.not. named .or. name /= path) error stop
  if (access /= 'SEQUENTIAL' .or. sequential /= 'YES' .or. direct /= 'NO') error stop
  if (form /= 'FORMATTED' .or. formatted /= 'YES' .or. unformatted /= 'NO') error stop
  if (blank /= 'NULL' .or. action /= 'READWRITE') error stop
  if (position /= 'APPEND') error stop
  if (readable /= 'YES' .or. writable /= 'YES' .or. readwrite /= 'YES') error stop
  if (delim /= 'QUOTE' .or. pad /= 'YES') error stop

  close(unit=21, status='delete', iostat=status, iomsg=message)
  if (status /= 0) error stop
  inquire(file=path, exist=exist, opened=opened, iostat=status, iomsg=message)
  if (status /= 0 .or. exist .or. opened) error stop

  open(unit=22, file='f2c-file-status.tmp', status='new', action='write', &
       position='append', iostat=status, iomsg=message)
  if (status /= 0) error stop
  write(22, *) 42
  close(22, status='keep', iostat=status, iomsg=message)
  if (status /= 0) error stop
  open(unit=22, file='f2c-file-status.tmp', status='old', action='read', &
       position='rewind', iostat=status, iomsg=message)
  if (status /= 0) error stop
  read(22, *, iostat=status, iomsg=message) value
  if (status /= 0 .or. value /= 42) error stop
  close(22, status='delete', iostat=status, iomsg=message)
  if (status /= 0) error stop

  open(unit=23, status='scratch', action='readwrite', iostat=status, iomsg=message)
  if (status /= 0) error stop
  inquire(unit=23, opened=opened, named=named, iostat=status, iomsg=message)
  if (status /= 0 .or. .not. opened .or. named) error stop
  close(23, iostat=status, iomsg=message)
  if (status /= 0) error stop

  open(unit=24, file='f2c-file-unknown.tmp', status='unknown', &
       iostat=status, iomsg=message)
  if (status /= 0) error stop
  close(24, status='delete', iostat=status, iomsg=message)
  if (status /= 0) error stop

  dynamic_status = 'invalid'
  open(unit=25, file='f2c-file-invalid.tmp', status=dynamic_status, &
       iostat=status, iomsg=message)
  if (status == 0) error stop
  write(*, '(A)') 'file-control ok'
end program file_control
