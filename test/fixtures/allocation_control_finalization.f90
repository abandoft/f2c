module allocation_control_final_cases
  implicit none
  integer :: finals = 0, phase = 0
  integer, allocatable :: first(:), second(:)
  integer :: statuses(3)
  character(len=40) :: messages(3)
  character(len=:), allocatable :: text, other_text
  character(len=4), allocatable :: fixed
  type :: control
    integer, allocatable :: number
  contains
    final :: finish
  end type
contains
  function owned(n) result(value)
    integer, intent(in) :: n
    type(control), allocatable :: value
    allocate(value)
    allocate(value%number, source=n)
  end function
  integer function integer_value(value)
    type(control), intent(in) :: value
    integer_value = value%number
  end function
  subroutine finish(value)
    type(control), intent(inout) :: value
    if (.not.allocated(value%number)) stop 90
    if (phase == 1) then
      if (.not.allocated(first) .or. .not.allocated(second)) stop 91
      if (statuses(2) /= 0) stop 92
    else if (phase == 2) then
      if (.not.allocated(text) .or. .not.allocated(other_text)) stop 93
      if (len(text) /= 4 .or. len(other_text) /= 4 .or. statuses(2) /= 0) stop 94
    else if (phase == 3) then
      if (allocated(fixed) .or. statuses(2) <= 0) stop 95
    else if (phase == 4) then
      if (allocated(first) .or. allocated(second) .or. statuses(2) /= 0) stop 96
    end if
    finals = finals + 1
  end subroutine
end module
program allocation_control_finalization
  use allocation_control_final_cases
  implicit none
  statuses = -99
  messages = 'untouched'
  phase = 1
  allocate(first(integer_value(owned(-2)):integer_value(owned(2))), &
           second(integer_value(owned(3))), &
           stat=statuses(integer_value(owned(2))), &
           errmsg=messages(integer_value(owned(2))))
  if (finals /= 5 .or. size(first) /= 5 .or. size(second) /= 3) stop 1
  if (statuses(2) /= 0 .or. any(messages /= 'untouched')) stop 2
  phase = 2
  allocate(character(len=integer_value(owned(4))) :: text, other_text, &
           stat=statuses(integer_value(owned(2))))
  if (finals /= 7 .or. len(text) /= 4 .or. len(other_text) /= 4) stop 3
  phase = 3
  allocate(character(len=integer_value(owned(3))) :: fixed, &
           stat=statuses(integer_value(owned(2))), &
           errmsg=messages(integer_value(owned(2))))
  if (finals /= 10 .or. statuses(2) <= 0 .or. allocated(fixed)) stop 4
  if (messages(2) == 'untouched') stop 5
  phase = 4
  deallocate(first, second, stat=statuses(integer_value(owned(2))), &
             errmsg=messages(integer_value(owned(2))))
  if (finals /= 12 .or. allocated(first) .or. allocated(second)) stop 6
  phase = 0
  deallocate(text, other_text)
  print '(A)', 'allocation control finalization passed'
end program
