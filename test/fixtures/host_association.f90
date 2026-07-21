program host_association
  implicit none(type, external)
  integer :: counter, values(4), limit, scratch_total, shadowed_host
  integer, allocatable :: dynamic_values(:)
  character(len=5) :: text
  external :: host_dummy_capture
  interface
    subroutine host_descriptor_capture(values)
      integer, allocatable, intent(inout) :: values(:)
    end subroutine host_descriptor_capture
    integer function host_external_increment(value) result(answer)
      integer, intent(in) :: value
    end function host_external_increment
  end interface

  counter = 3
  values = [1, 2, 3, 4]
  text = 'abc'
  limit = 3
  scratch_total = 0
  shadowed_host = 5
  allocate(dynamic_values(2))
  dynamic_values = [6, 7]

  call driver()
  call forward_through_shadow()
  call use_specification_expression()
  call host_dummy_capture(counter, values)
  call host_descriptor_capture(dynamic_values)

  if (counter /= 10) error stop 1
  if (any(values /= [1, 9, 8, 4])) error stop 2
  if (text /= 'abcxy') error stop 3
  if (recursive_value(2) /= 12) error stop 4
  if (scratch_total /= 9) error stop 5
  if (call_host_external() /= 5) error stop 6
  if (any(dynamic_values /= [6, 12])) error stop 7
  if (shadowed_host /= 6) error stop 8
  call shadow_counter()
  if (counter /= 10) error stop 9

  write (*, '(I0,1X,4(I0,1X),A,1X,I0)') counter, values, text, scratch_total

contains

  subroutine driver()
    call mutate(2)
  end subroutine driver

  subroutine mutate(delta)
    integer, intent(in) :: delta
    counter = counter + delta
    values(2) = values(2) + delta
    text(4:5) = 'xy'
  end subroutine mutate

  recursive integer function recursive_value(remaining) result(value)
    integer, intent(in) :: remaining
    if (remaining == 0) then
      value = counter
    else
      value = recursive_value(remaining - 1) + 1
    end if
  end function recursive_value

  subroutine use_specification_expression()
    integer :: scratch(limit)
    scratch = limit
    scratch_total = sum(scratch)
  end subroutine use_specification_expression

  subroutine shadow_counter()
    integer :: counter
    counter = 99
    if (counter /= 99) error stop 10
  end subroutine shadow_counter

  subroutine forward_through_shadow()
    integer :: shadowed_host
    shadowed_host = 99
    call increment_shadowed_host()
    if (shadowed_host /= 99) error stop 11
  end subroutine forward_through_shadow

  subroutine increment_shadowed_host()
    shadowed_host = shadowed_host + 1
  end subroutine increment_shadowed_host

  integer function call_host_external() result(value)
    value = host_external_increment(4)
  end function call_host_external

end program host_association

subroutine host_dummy_capture(number, array)
  implicit none(type, external)
  integer, intent(inout) :: number
  integer, intent(inout) :: array(4)

  call inner()

contains

  subroutine inner()
    number = number + 5
    array(2) = array(2) + 5
    array(3) = array(3) + 5
  end subroutine inner

end subroutine host_dummy_capture

subroutine host_descriptor_capture(values)
  implicit none(type, external)
  integer, allocatable, intent(inout) :: values(:)

  call inner()

contains

  subroutine inner()
    values(2) = values(2) + 5
  end subroutine inner

end subroutine host_descriptor_capture

integer function host_external_increment(value) result(answer)
  implicit none(type, external)
  integer, intent(in) :: value
  answer = value + 1
end function host_external_increment
