module argument_attribute_support
  implicit none

  integer, volatile :: shared_value

  type :: box
    integer, allocatable :: data(:)
  end type box

contains

  subroutine mutate_copies(number, text, object)
    integer, value :: number
    character(4), value :: text
    type(box), value :: object

    number = 99
    text = 'zz'
    object%data(1) = 99
  end subroutine mutate_copies

  subroutine mutate_target_copy(number, result)
    integer :: number
    value :: number
    target :: number
    integer, intent(out) :: result
    integer, pointer :: alias

    alias => number
    alias = 42
    result = number
  end subroutine mutate_target_copy

  subroutine inspect_optional(number, was_present)
    integer, value, optional :: number
    logical, intent(out) :: was_present

    was_present = present(number)
    if (was_present) number = number + 1
  end subroutine inspect_optional

  subroutine update_observable(first, second)
    integer, volatile, intent(inout) :: first
    integer, asynchronous, target, intent(inout) :: second

    first = first + 1
    second = second + 1
  end subroutine update_observable

end module argument_attribute_support

program argument_attributes
  use argument_attribute_support
  implicit none

  abstract interface
    subroutine value_callback(item)
      integer, value :: item
    end subroutine value_callback
  end interface

  integer :: number
  integer :: copied_result
  integer, volatile :: first
  integer, asynchronous, target :: second
  character(4) :: text
  type(box) :: object
  logical :: was_present
  procedure(value_callback), pointer :: callback

  number = 7
  text = 'abcd'
  allocate(object%data(1))
  object%data(1) = 11
  call mutate_copies(number, text, object)
  if (number /= 7) error stop 1
  if (text /= 'abcd') error stop 2
  if (object%data(1) /= 11) error stop 3

  call mutate_target_copy(number, copied_result)
  if (number /= 7 .or. copied_result /= 42) error stop 4

  call inspect_optional(was_present=was_present)
  if (was_present) error stop 5
  call inspect_optional(number, was_present)
  if (.not. was_present .or. number /= 7) error stop 6

  callback => mutate_through_pointer
  call callback(number)
  if (number /= 7) error stop 7

  first = 1
  second = 2
  call update_observable(first, second)
  if (first /= 2 .or. second /= 3) error stop 8

contains

  subroutine mutate_through_pointer(item)
    integer, value :: item

    item = 100
  end subroutine mutate_through_pointer
end program argument_attributes
