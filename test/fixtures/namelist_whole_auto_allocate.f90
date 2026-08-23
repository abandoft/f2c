program namelist_whole_auto_allocate
  implicit none

  integer, allocatable :: dynamic(:)
  integer :: status
  character(128) :: record
  namelist /sample/ dynamic

  record = '&sample dynamic=3*7,,9 /'
  read(record, nml=sample, iostat=status)

  if (status /= 0) stop 1
  if (.not. allocated(dynamic)) stop 2
  if (lbound(dynamic, 1) /= 1 .or. size(dynamic) /= 5) stop 3
  if (any(dynamic /= [7, 7, 7, 0, 9])) stop 4
end program namelist_whole_auto_allocate
