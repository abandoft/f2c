program namelist_derived_array
  implicit none

  type :: pair
    integer :: key
    integer :: value
  end type pair

  type(pair) :: entries(2)
  integer :: status
  character(256) :: record
  namelist /sample/ entries

  entries(1)%key = 0
  entries(1)%value = 0
  entries(2)%key = 0
  entries(2)%value = 0
  record = '&sample entries=1,2,3,4 /'
  read(record, nml=sample, iostat=status)
  if (status /= 0) stop 1
  if (entries(1)%key /= 1 .or. entries(1)%value /= 2) stop 2
  if (entries(2)%key /= 3 .or. entries(2)%value /= 4) stop 3

  record = '&sample entries(2:1:-1)%key=30,10 /'
  read(record, nml=sample, iostat=status)
  if (status /= 0) stop 4
  if (entries(1)%key /= 10 .or. entries(2)%key /= 30) stop 5
  write(*, '(4(I0,1X))') entries(1)%key, entries(1)%value, &
                            entries(2)%key, entries(2)%value
end program namelist_derived_array
