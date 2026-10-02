program character_actual_evaluation
  implicit none
  character(8) :: records(2,2)
  integer :: index_calls, lower_calls, upper_calls, result
  records = 'abcdefgh'
  index_calls = 0
  lower_calls = 0
  upper_calls = 0
  call edit(records(choose_index(),2:1:-1)(choose_lower():choose_upper()))
  if (index_calls /= 1 .or. lower_calls /= 1 .or. upper_calls /= 1) stop 1
  if (any(records(1,:) /= 'aXYZefgh') .or. any(records(2,:) /= 'abcdefgh')) stop 2
  index_calls = 0
  lower_calls = 0
  upper_calls = 0
  result = edit_length(records(choose_index(),:)(choose_lower():choose_upper()))
  if (index_calls /= 1 .or. lower_calls /= 1 .or. upper_calls /= 1) stop 3
  if (result /= 3 .or. any(records(1,:) /= 'aFUNefgh')) stop 4
contains
  subroutine edit(values)
    character(*), intent(out), contiguous :: values(:)
    values = 'XYZ'
  end subroutine
  integer function edit_length(values)
    character(*), intent(inout) :: values(:)
    values = 'FUN'
    edit_length = len(values)
  end function
  integer function choose_index()
    index_calls = index_calls + 1
    choose_index = 1
  end function
  integer function choose_lower()
    lower_calls = lower_calls + 1
    choose_lower = 2
  end function
  integer function choose_upper()
    upper_calls = upper_calls + 1
    choose_upper = 4
  end function
end program
