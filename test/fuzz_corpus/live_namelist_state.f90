0module namelist_state_seed
contains
  subroutine read_values(values)
    integer, allocatable, volatile, intent(inout) :: values(:)
    integer :: status
    character(64) :: record
    namelist /state/ values
    record = '&state values=7,8,9 /'
    read(record,nml=state,iostat=status)
    if (status /= 0 .or. values(0) /= 8) stop 1
    record = '&state values=12,bad,14 /'
    read(record,nml=state,iostat=status)
    if (status == 0 .or. any(values /= [7,8,9])) stop 2
    write(record,nml=state,iostat=status)
    if (status /= 0) stop 3
  end subroutine
end module
program namelist_state_main
  use namelist_state_seed
  integer, allocatable :: values(:)
  allocate(values(-1:1))
  call read_values(values)
  deallocate(values)
end program
