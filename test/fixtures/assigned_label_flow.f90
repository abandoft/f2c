program assigned_label_flow
  implicit none
  integer :: value

  call route(.true., value)
  write (*, '(I0)') value
  call route(.false., value)
  write (*, '(I0)') value
  call loop_route(.true., value)
  write (*, '(I0)') value
  call loop_route(.false., value)
  write (*, '(I0)') value
  call format_route(.true., 7)
  call format_route(.false., 8)
end program assigned_label_flow

subroutine route(flag, value)
  implicit none
  logical, intent(in) :: flag
  integer, intent(out) :: value
  integer :: target

  if (flag) then
    assign 100 to target
  else
    assign 200 to target
  end if
  go to target, (100, 100, 200)
100 value = 11
  go to 300
200 value = 22
300 continue
end subroutine route

subroutine loop_route(flag, value)
  implicit none
  logical, intent(in) :: flag
  integer, intent(out) :: value
  integer :: iteration, target

  assign 100 to target
  do iteration = 1, 2
    if (flag .and. iteration == 2) assign 200 to target
  end do
  go to target
100 value = 33
  go to 300
200 value = 44
300 continue
end subroutine loop_route

subroutine format_route(flag, value)
  implicit none
  logical, intent(in) :: flag
  integer, intent(in) :: value
  integer :: fmt

  if (flag) then
    assign 400 to fmt
  else
    assign 500 to fmt
  end if
  write (*, fmt) value
400 format('A:', I0)
500 format('B:', I0)
end subroutine format_route
