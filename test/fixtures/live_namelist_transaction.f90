module live_namelist_transaction_model
contains
  subroutine read_state(values)
    integer, allocatable, volatile, intent(inout) :: values(:)
    integer :: status
    character(64) :: record
    namelist /state/ values
    record = '&state values=12,bad,14 /'
    read(record,nml=state,iostat=status)
    if (status == 0) stop 1
    ! f2c's transactional input contract is independently asserted: Fortran
    ! leaves affected data undefined on an input error, so native old/new values
    ! are not a numerical oracle for this additional failure-preservation rule.
    if (.not. allocated(values)) stop 2
    if (lbound(values,1) /= -1 .or. ubound(values,1) /= 1) stop 3
    if (any(values /= [7,8,9])) stop 4
  end subroutine read_state
end module live_namelist_transaction_model

program live_namelist_transaction
  use live_namelist_transaction_model
  integer, allocatable, volatile :: values(:)
  allocate(values(-1:1))
  values = [7,8,9]
  call read_state(values)
  if (any(values /= [7,8,9])) stop 5
  deallocate(values)
  print '(a)', 'live namelist transaction contracts passed'
end program live_namelist_transaction
