module live_object_state_model
  implicit none
  integer :: length_evaluations = 0
  type :: object
    integer, pointer :: values(:) => null()
    character(:), allocatable :: text
  end type object
contains
  integer function next_result_length() result(length)
    length_evaluations = length_evaluations + 1
    length = 2
  end function next_result_length

  function state_character_value(values, length) result(text)
    integer, pointer, intent(inout) :: values(:)
    integer, intent(in) :: length
    character(length) :: text
    values(1) = 47
    text = 'ok'
  end function state_character_value

  function state_derived_value(values) result(copy)
    integer, pointer, intent(inout) :: values(:)
    type(object) :: copy
    values(1) = 49
    copy%text = 'owned'
    copy%values => values
  end function state_derived_value

  subroutine retarget(values, target)
    integer, pointer, volatile, intent(inout) :: values(:)
    integer, target, intent(inout) :: target(:)
    values(-2:) => target(2:6:2)
  end subroutine retarget

  integer function retarget_value(values, target) result(total)
    integer, pointer, volatile, intent(inout) :: values(:)
    integer, target, intent(inout) :: target(:)
    call retarget(values, target)
    total = sum(values)
  end function retarget_value

  subroutine observe_pointer(values, target)
    integer, pointer, volatile, intent(inout) :: values(:)
    integer, target, intent(inout) :: target(:)
    integer :: total
    if (size(values) /= 2 .or. lbound(values,1) /= 1) stop 1
    call retarget(values, target)
    if (size(values) /= 3 .or. lbound(values,1) /= -2) stop 2
    if (ubound(values,1) /= 0 .or. values(-2) /= 20) stop 3
    if (sum(values) /= 120) stop 4
    nullify(values)
    if (associated(values)) stop 5
    total = retarget_value(values, target)
    if (total /= 120 .or. size(values) /= 3) stop 6
    if (values(0) /= 60 .or. lbound(values,1) /= -2) stop 7
  end subroutine observe_pointer

  subroutine fill_allocatable(values)
    integer, allocatable, volatile, intent(inout) :: values(:)
    if (allocated(values)) deallocate(values)
    allocate(values(-1:1))
    values = [4,5,6]
  end subroutine fill_allocatable

  integer function fill_value(values) result(total)
    integer, allocatable, volatile, intent(inout) :: values(:)
    call fill_allocatable(values)
    total = sum(values)
  end function fill_value

  subroutine observe_allocatable(values)
    integer, allocatable, volatile, intent(inout) :: values(:)
    integer, allocatable :: moved(:)
    integer :: total
    if (allocated(values)) stop 8
    call fill_allocatable(values)
    if (.not. allocated(values)) stop 9
    if (lbound(values,1) /= -1 .or. values(0) /= 5) stop 10
    call move_alloc(values, moved)
    if (allocated(values) .or. .not. allocated(moved)) stop 11
    if (sum(moved) /= 15 .or. lbound(moved,1) /= -1) stop 12
    total = fill_value(values)
    if (total /= 15 .or. .not. allocated(values)) stop 13
    if (size(values) /= 3 .or. values(1) /= 6) stop 14
    deallocate(values, moved)
  end subroutine observe_allocatable

  subroutine retarget_text(text, target)
    character(:), pointer, volatile, intent(inout) :: text
    character(*), target, intent(inout) :: target
    text => target
  end subroutine retarget_text

  integer function text_value(text, target) result(length)
    character(:), pointer, volatile, intent(inout) :: text
    character(*), target, intent(inout) :: target
    call retarget_text(text, target)
    length = len(text)
  end function text_value

  subroutine observe_text(text, target)
    character(:), pointer, volatile, intent(inout) :: text
    character(*), target, intent(inout) :: target
    integer :: length
    if (len(text) /= 2) stop 15
    call retarget_text(text, target)
    if (len(text) /= 5 .or. text /= 'live!') stop 16
    nullify(text)
    if (associated(text)) stop 17
    length = text_value(text, target)
    if (length /= 5 .or. text /= 'live!') stop 18
  end subroutine observe_text

  integer function set_text(text) result(length)
    character(:), allocatable, volatile, intent(inout) :: text
    text = 'longer'
    length = len(text)
  end function set_text

  function retarget_character(text, target) result(value)
    character(:), pointer, volatile, intent(inout) :: text
    character(*), target, intent(inout) :: target
    character(2) :: value
    call retarget_text(text, target)
    value = 'ok'
  end function retarget_character

  function fill_array_value(values) result(copy)
    integer, allocatable, volatile, intent(inout) :: values(:)
    integer, allocatable :: copy(:)
    call fill_allocatable(values)
    copy = values + 10
  end function fill_array_value

  subroutine clear_out(values)
    integer, allocatable, volatile, optional, intent(out) :: values(:)
    if (present(values)) then
      if (allocated(values)) stop 19
      allocate(values(2:1))
      if (.not. allocated(values) .or. size(values) /= 0) stop 20
    end if
  end subroutine clear_out

  subroutine check_input(values, target)
    integer, pointer, intent(in) :: values(:)
    integer, target, intent(inout) :: target(:)
    values(1) = 37
    if (target(1) /= 37) stop 21
  end subroutine check_input

  subroutine read_allocatable(values)
    integer, allocatable, volatile, intent(inout) :: values(:)
    integer :: status
    character(64) :: record
    namelist /state/ values
    record = '&state values=7,8,9 /'
    read(record,nml=state,iostat=status)
    if (status /= 0 .or. values(0) /= 8) stop 36
    record = '&state values=12,bad,14 /'
    read(record,nml=state,iostat=status)
    if (status == 0) stop 37
    ! The standard does not specify the data values after an input error.
    ! Re-establish them before comparing numerical results with native Fortran.
    record = '&state values=7,8,9 /'
    read(record,nml=state,iostat=status)
    if (status /= 0 .or. sum(values) /= 24 .or. lbound(values,1) /= -1) stop 38
    write(record,nml=state,iostat=status)
    if (status /= 0) stop 40
    values = 0
    read(record,nml=state,iostat=status)
    if (status /= 0 .or. values(-1) /= 7 .or. values(1) /= 9) stop 41
    write(record,'(3i3)') values
    if (record(:9) /= '  7  8  9') stop 39
  end subroutine read_allocatable
end module live_object_state_model

program live_object_state
  use live_object_state_model
  implicit none
  integer, target :: original(2) = [1,2]
  integer, target :: replacement(6) = [10,20,30,40,50,60]
  integer, pointer, volatile :: values(:)
  integer, allocatable, volatile :: allocated_values(:)
  character(2), target :: old_text = 'ab'
  character(5), target :: new_text = 'live!'
  character(:), pointer, volatile :: text
  character(2) :: returned_text
  integer, allocatable :: returned_array(:)
  type(object), volatile :: instance
  type(object) :: returned_object
  type(object) :: f2c_deferred_owner, f2c_deferred_owner_0
  character(:), allocatable :: f2c_deferred_value, f2c_deferred_source, f2c_deferred_length
  character(:), allocatable :: f2c_deferred_value_0, f2c_deferred_source_0, f2c_deferred_length_0
  integer :: total, length

  values => original
  call observe_pointer(values, replacement)
  if (size(values) /= 3 .or. values(0) /= 60) stop 22
  nullify(values)
  total = retarget_value(values, replacement)
  if (total /= 120 .or. lbound(values,1) /= -2) stop 23
  total = retarget_value(instance%values, replacement)
  if (total /= 120 .or. instance%values(-2) /= 20) stop 24
  if (size(instance%values) /= 3) stop 25

  call observe_allocatable(allocated_values)
  if (allocated(allocated_values)) stop 26
  total = fill_value(allocated_values)
  if (total /= 15 .or. allocated_values(-1) /= 4) stop 27
  call read_allocatable(allocated_values)
  call clear_out(allocated_values)
  if (.not. allocated(allocated_values) .or. size(allocated_values) /= 0) stop 28
  deallocate(allocated_values)
  call clear_out()
  returned_array = fill_array_value(allocated_values)
  if (.not. allocated(allocated_values) .or. allocated_values(0) /= 5) stop 33
  if (size(returned_array) /= 3 .or. sum(returned_array) /= 45) stop 34
  total = sum(fill_array_value(allocated_values))
  if (total /= 45 .or. allocated_values(0) /= 5) stop 43
  deallocate(allocated_values, returned_array)

  text => old_text
  call observe_text(text, new_text)
  if (len(text) /= 5 .or. text /= 'live!') stop 29
  text => old_text
  returned_text = retarget_character(text, new_text)
  if (returned_text /= 'ok' .or. len(text) /= 5) stop 35
  length = set_text(instance%text)
  if (length /= 6 .or. len(instance%text) /= 6) stop 30
  if (instance%text /= 'longer') stop 31
  deallocate(instance%text)

  values => original
  returned_text = state_character_value(values, next_result_length())
  if (length_evaluations /= 1 .or. original(1) /= 47 .or. returned_text /= 'ok') stop 42
  returned_object = state_derived_value(values)
  if (original(1) /= 49 .or. returned_object%text /= 'owned') stop 44
  if (.not. associated(returned_object%values, values)) stop 45
  returned_object%text = returned_object%text // '!'
  if (len(returned_object%text) /= 6 .or. returned_object%text /= 'owned!') stop 46
  instance%text = 'qualified'
  returned_object%text = instance%text
  if (len(returned_object%text) /= 9 .or. returned_object%text /= 'qualified') stop 47
  returned_object%text = ''
  if (.not. allocated(returned_object%text) .or. len(returned_object%text) /= 0) stop 48
  deallocate(instance%text)
  deallocate(returned_object%text)
  nullify(returned_object%values)
  call check_input(values, original)
  if (original(1) /= 37) stop 32
  nullify(values, instance%values, text)
  f2c_deferred_owner%text = 'owner'
  f2c_deferred_owner_0%text = 'owner0'
  f2c_deferred_value = 'value'
  f2c_deferred_source = 'source'
  f2c_deferred_length = 'length'
  f2c_deferred_value_0 = 'value0'
  f2c_deferred_source_0 = 'source0'
  f2c_deferred_length_0 = 'length0'
  if (f2c_deferred_owner%text /= 'owner' .or. f2c_deferred_owner_0%text /= 'owner0') stop 50
  if (f2c_deferred_value /= 'value' .or. f2c_deferred_value_0 /= 'value0') stop 51
  if (f2c_deferred_source /= 'source' .or. f2c_deferred_source_0 /= 'source0') stop 52
  if (f2c_deferred_length /= 'length' .or. f2c_deferred_length_0 /= 'length0') stop 53
  deallocate(f2c_deferred_owner%text, f2c_deferred_owner_0%text)
  deallocate(f2c_deferred_value, f2c_deferred_source, f2c_deferred_length)
  deallocate(f2c_deferred_value_0, f2c_deferred_source_0, f2c_deferred_length_0)
  print '(a)', 'live object state contracts passed'
end program live_object_state
