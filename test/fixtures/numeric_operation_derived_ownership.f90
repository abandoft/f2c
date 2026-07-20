module numeric_operation_final_state
  implicit none

  integer :: finalized = 0

  type :: tracked_pair
    integer :: value
  contains
    procedure :: add_value => tracked_pair_add_value
    procedure :: forward => tracked_pair_forward
    final :: finalize_tracked_pair
  end type tracked_pair

contains

  subroutine finalize_tracked_pair(object)
    type(tracked_pair), intent(inout) :: object

    finalized = finalized + 1
    object%value = 0
  end subroutine finalize_tracked_pair

  function make_tracked_pair(value) result(object)
    integer, intent(in) :: value
    type(tracked_pair) :: object

    object%value = value
  end function make_tracked_pair

  integer function tracked_pair_value(object)
    type(tracked_pair), intent(in) :: object

    tracked_pair_value = object%value
  end function tracked_pair_value

  integer function tracked_pair_add_value(self, other)
    class(tracked_pair), intent(in) :: self
    type(tracked_pair), intent(in) :: other

    tracked_pair_add_value = self%value + other%value
  end function tracked_pair_add_value

  character(len=3) function tracked_pair_label(object)
    type(tracked_pair), intent(in) :: object

    if (object%value == 62) then
      tracked_pair_label = 'YES'
    else
      tracked_pair_label = 'NO '
    end if
  end function tracked_pair_label

  function tracked_pair_forward(self, other) result(output)
    class(tracked_pair), intent(in) :: self
    type(tracked_pair), intent(in) :: other
    type(tracked_pair) :: output

    output%value = self%value + other%value
  end function tracked_pair_forward
end module numeric_operation_final_state

program numeric_operation_derived_ownership
  use numeric_operation_final_state
  implicit none

  type :: pair
    integer :: first
    integer :: second
    integer, allocatable :: payload(:)
  end type pair

  logical :: choose, active(4), branches(4)
  type(pair) :: selected(4)
  type(tracked_pair) :: tracker

  choose = .true.
  selected = merge(make_pair(15, 16, 150), make_pair(17, 18, 170), choose)
  if (selected(1)%first /= 15 .or. selected(4)%second /= 16 .or. &
      selected(2)%payload(1) /= 150 .or. selected(3)%payload(2) /= 151) error stop 1

  choose = .false.
  selected = merge(make_pair(19, 20, 190), &
    merge(make_pair(21, 22, 210), make_pair(23, 24, 230), .false.), choose)
  if (selected(1)%first /= 23 .or. selected(4)%second /= 24 .or. &
      selected(2)%payload(1) /= 230 .or. selected(3)%payload(2) /= 231) error stop 2

  call verify_pair(merge(make_pair(25, 26, 250), make_pair(27, 28, 270), .true.), &
    25, 26, 250, 251)
  selected = [ &
    merge(make_pair(29, 30, 290), make_pair(31, 32, 310), .true.), &
    merge(make_pair(33, 34, 330), make_pair(35, 36, 350), .false.), &
    merge(make_pair(37, 38, 370), make_pair(39, 40, 390), .true.), &
    merge(make_pair(41, 42, 410), make_pair(43, 44, 430), .false.)]
  if (selected(1)%first /= 29 .or. selected(2)%second /= 36 .or. &
      selected(3)%payload(1) /= 370 .or. selected(4)%payload(2) /= 431) error stop 3

  active = [.true., .true., .true., .true.]
  branches = [.true., .false., .false., .true.]
  where (active)
    selected = merge(make_pair(45, 46, 450), make_pair(47, 48, 470), branches)
  end where
  if (selected(1)%first /= 45 .or. selected(2)%second /= 48 .or. &
      selected(3)%payload(1) /= 470 .or. selected(4)%payload(2) /= 451) error stop 4

  call verify_pairs(merge(make_pair(49, 50, 490), selected, branches))
  if (selected(2)%payload(1) /= 470 .or. selected(3)%payload(2) /= 471) error stop 5
  if (pair_sum(merge(make_pair(51, 52, 510), make_pair(53, 54, 530), .false.)) /= &
      53 + 54 + 530 + 531) error stop 6
  if (tracked_pair_value(merge(make_tracked_pair(55), make_tracked_pair(56), .true.)) /= 55) &
    error stop 7
  if (finalized /= 1) error stop 8
  tracker%value = 57
  if (tracker%add_value(merge(make_tracked_pair(58), make_tracked_pair(59), .false.)) /= 116) &
    error stop 9
  if (finalized /= 2) error stop 10
  tracker%value = 60
  if (tracked_pair_value(merge(tracker, make_tracked_pair(61), .true.)) /= 60) error stop 11
  if (tracker%value /= 60 .or. finalized /= 3) error stop 12
  if (tracked_pair_label(merge(make_tracked_pair(62), make_tracked_pair(63), .true.)) /= &
      'YES') error stop 13
  if (finalized /= 4) error stop 14
  if (tracked_pair_value( &
      tracker%forward(merge(make_tracked_pair(64), make_tracked_pair(65), .false.))) /= 125) &
    error stop 15
  if (finalized /= 6) error stop 16
  selected(1) = forward_pair(merge(make_pair(66, 67, 660), make_pair(68, 69, 680), .false.))
  if (selected(1)%first /= 68 .or. selected(1)%second /= 69 .or. &
      selected(1)%payload(1) /= 680 .or. selected(1)%payload(2) /= 681) error stop 17

  print '(A)', 'NUMERIC-DERIVED-OWNERSHIP-PASS'

contains

  function make_pair(first, second, payload_base) result(value)
    integer, intent(in) :: first, second, payload_base
    type(pair) :: value

    value%first = first
    value%second = second
    value%payload = [payload_base, payload_base + 1]
  end function make_pair

  subroutine verify_pair(value, expected_first, expected_second, expected_payload_1, &
                         expected_payload_2)
    type(pair), intent(in) :: value
    integer, intent(in) :: expected_first, expected_second
    integer, intent(in) :: expected_payload_1, expected_payload_2

    if (value%first /= expected_first .or. value%second /= expected_second .or. &
        value%payload(1) /= expected_payload_1 .or. &
        value%payload(2) /= expected_payload_2) error stop 18
  end subroutine verify_pair

  subroutine verify_pairs(values)
    type(pair), intent(in) :: values(:)

    if (values(1)%first /= 49 .or. values(2)%first /= 47 .or. &
        values(3)%payload(1) /= 470 .or. values(4)%payload(2) /= 491) error stop 19
  end subroutine verify_pairs

  integer function pair_sum(value)
    type(pair), intent(in) :: value

    pair_sum = value%first + value%second + value%payload(1) + value%payload(2)
  end function pair_sum

  function forward_pair(value) result(output)
    type(pair), intent(in) :: value
    type(pair) :: output

    output = value
  end function forward_pair
end program numeric_operation_derived_ownership
