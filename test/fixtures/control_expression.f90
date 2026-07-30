module control_expression_cases
  implicit none

  integer :: calls = 0
  integer, target :: storage(5) = [1, 2, 3, 4, 5]

contains

  function explicit_values() result(values)
    integer :: values(3)

    calls = calls + 1
    values = [1, 2, 3]
  end function explicit_values

  function allocated_values() result(values)
    integer, allocatable :: values(:)

    calls = calls + 1
    allocate(values(-1:1))
    values = [4, 5, 6]
  end function allocated_values

  function loop_values(value) result(values)
    integer, intent(in) :: value
    integer, allocatable :: values(:)

    calls = calls + 1
    allocate(values(2))
    values = [value, value + 1]
  end function loop_values

  function selected_values() result(values)
    integer, pointer :: values(:)

    calls = calls + 1
    values => storage(5:1:-2)
  end function selected_values

  function words() result(values)
    character(len=3) :: values(2)

    calls = calls + 1
    values = ["one", "two"]
  end function words

  subroutine choose_first(*, *)
    return sum(explicit_values()) - 5
  end subroutine choose_first

end module control_expression_cases

program control_expression
  use control_expression_cases
  implicit none

  integer :: branch, counted_iterations, index, iterations

  calls = 0
  branch = 0
  if (all(explicit_values() == [1, 2, 3])) then
    branch = branch + 1
  end if
  if (branch /= 1 .or. calls /= 1) error stop 1

  calls = 0
  branch = 0
  if (any(explicit_values() == 9)) then
    branch = -1
  else if (any(allocated_values() == 8)) then
    branch = -2
  else if (all(explicit_values() == [1, 2, 3])) then
    branch = 2
  else
    branch = -3
  end if
  if (branch /= 2 .or. calls /= 3) error stop 2

  calls = 0
  branch = 0
  if (any(explicit_values() == 1)) then
    branch = 3
  else if (any(allocated_values() == 4)) then
    branch = -4
  end if
  if (branch /= 3 .or. calls /= 1) error stop 3

  calls = 0
  if (all(reshape(explicit_values(), [3]) == [1, 2, 3])) branch = branch + 1
  if (branch /= 4 .or. calls /= 1) error stop 4

  calls = 0
  iterations = 0
  do while (all(loop_values(iterations) < 3))
    iterations = iterations + 1
  end do
  if (iterations /= 2 .or. calls /= 3) error stop 5

  calls = 0
  counted_iterations = 0
  do index = sum(explicit_values()) - 5, sum(allocated_values()) - 9, &
      sum(explicit_values()) - 5
    counted_iterations = counted_iterations + 1
  end do
  if (counted_iterations /= 6 .or. calls /= 3) error stop 13

  calls = 0
  if (any(selected_values() /= [5, 3, 1])) error stop 6
  if (calls /= 1 .or. any(storage /= [1, 2, 3, 4, 5])) error stop 7

  calls = 0
  if (.not. all(words() == ["one", "two"])) error stop 8
  if (calls /= 1) error stop 9

  calls = 0
  if (sum(explicit_values()) - 6) 100, 110, 120
100 error stop 10
110 branch = branch + 1
  go to 130
120 error stop 11
130 continue
  if (branch /= 5 .or. calls /= 1) error stop 12

  calls = 0
  go to (140, 150) sum(explicit_values()) - 5
140 branch = branch + 1
  go to 160
150 error stop 17
160 continue
  if (branch /= 6 .or. calls /= 1) error stop 18

  calls = 0
  call choose_first(*170, *180)
  error stop 19
170 branch = branch + 1
  go to 190
180 error stop 20
190 continue
  if (branch /= 7 .or. calls /= 1) error stop 21

  calls = 0
  select case (sum(explicit_values()))
  case (5)
    error stop 14
  case (6)
    branch = branch + 1
  case default
    error stop 15
  end select
  if (branch /= 8 .or. calls /= 1) error stop 16

  if (calls < 0) stop sum(explicit_values())

  write (*, '(4(i0,1x))') branch, counted_iterations, iterations, sum(storage)
end program control_expression
