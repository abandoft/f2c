program namelist_invalid_designators
  implicit none

  integer :: values(3)
  integer :: status
  character(256) :: record
  namelist /sample/ values

  values = [1, 2, 3]
  record = '&sample values([1,3])=9,8 /'
  read(record, nml=sample, iostat=status)
  if (status == 0) stop 1
  if (any(values /= [1, 2, 3])) stop 2

  record = '&sample values(2:1)=9 /'
  read(record, nml=sample, iostat=status)
  if (status == 0) stop 3
  if (any(values /= [1, 2, 3])) stop 4

end program namelist_invalid_designators
