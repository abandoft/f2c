module character_length_storage
  implicit none
  character(len=-7) :: empty_scalar = 'discarded'
  character(len=-3) :: empty_array(2) = ['abc', 'xyz']
  character(len=-5), target :: empty_target
  character(len=-5), target :: empty_targets(2)
  character(:), allocatable :: allocated_array(:)
  type :: empty_record
    character(len=-4) :: label
    character(len=-9) :: pieces(2)
  end type
  integer :: length_calls = 0
contains
  integer function choose_length(value)
    integer, intent(in) :: value
    length_calls = length_calls + 1
    choose_length = value
  end function
  function make_text(n) result(value)
    integer, intent(in) :: n
    character(len=n) :: value
    value = 'X'
  end function
  function make_wide_text(n) result(value)
    integer(kind=8), intent(in) :: n
    character(len=n) :: value
    value = 'X'
  end function
  function take_piece(n, pieces) result(value)
    integer, intent(in) :: n
    character(*), intent(in) :: pieces(:)
    character(len=n) :: value
    value = pieces(1)
  end function
  subroutine check_automatic(n)
    integer, intent(in) :: n
    character(len=n) :: value, values(2)
    value = 'X'
    values = 'Y'
    if (len(value) /= max(0,n) .or. len(values) /= max(0,n)) stop 14
    if (n > 0) then
      if (value /= 'X' .or. any(values /= 'Y')) stop 15
    end if
  end subroutine
  subroutine try_length(n, status)
    integer(kind=8), intent(in) :: n
    integer, intent(out) :: status
    character(:), allocatable :: temporary
    allocate(character(len=n) :: temporary, stat=status)
    if (status == 0) deallocate(temporary)
  end subroutine
end module

program character_length_parameters
  use character_length_storage
  implicit none
  character(len=-8), parameter :: empty_constant = 'ignored'
  integer, parameter :: folded_length = len(empty_constant)
  character(len=-2), pointer :: empty_pointer
  character(len=-2), pointer :: empty_pointers(:)
  character(len=-1) :: local_scalar, local_array(2)
  character(:), allocatable :: value
  character(5) :: pieces(2)
  type(empty_record) :: record
  integer :: n, status
  integer(kind=8) :: wide

  if (folded_length /= 0 .or. len(empty_constant) /= 0) stop 1
  if (len(empty_scalar) /= 0 .or. len(empty_array) /= 0) stop 2
  empty_scalar = 'changed'
  empty_array = 'changed'
  local_scalar = 'changed'
  local_array = 'changed'
  if (len(local_scalar) /= 0 .or. len(local_array) /= 0) stop 3
  record%label = 'changed'
  record%pieces = 'changed'
  if (len(record%label) /= 0 .or. len(record%pieces) /= 0) stop 4
  empty_pointer => empty_target
  if (len(empty_pointer) /= 0 .or. .not. associated(empty_pointer)) stop 5
  if (associated(empty_pointer,empty_target)) stop 18
  nullify(empty_pointer)
  if (associated(empty_pointer,empty_pointer)) stop 19
  empty_pointers => empty_targets
  if (len(empty_pointers) /= 0 .or. .not. associated(empty_pointers)) stop 36
  if (associated(empty_pointers,empty_targets)) stop 37
  nullify(empty_pointers)

  length_calls = 0
  value = make_text(choose_length(-7))
  if (len(value) /= 0 .or. length_calls /= 1) stop 6
  n = 0
  value = make_text(n)
  if (len(value) /= 0) stop 7
  n = 3
  value = make_text(n)
  if (len(value) /= 3 .or. value /= 'X  ') stop 8
  wide = -huge(wide)
  value = make_wide_text(wide)
  if (len(value) /= 0) stop 9
  wide = 2_8
  value = make_wide_text(wide)
  if (len(value) /= 2 .or. value /= 'X ') stop 10
  pieces = ['aBCde','xYZuv']
  value = take_piece(-2,pieces(:)(2:3))
  if (len(value) /= 0 .or. any(pieces /= ['aBCde','xYZuv'])) stop 11
  value = take_piece(2,pieces(2:1:-1)(2:3))
  if (value /= 'YZ') stop 12
  call check_automatic(-7)
  call check_automatic(0)
  call check_automatic(1)
  n = -6
  allocate(character(len=n) :: value, stat=status)
  ! VALUE is already allocated: a failed allocation must leave its value unchanged.
  if (status == 0 .or. value /= 'YZ') stop 13
  deallocate(value)
  n = 0
  allocate(character(len=n) :: value, allocated_array(2), stat=status)
  if (status /= 0 .or. len(value) /= 0 .or. len(allocated_array) /= 0) stop 16
  value = 'ignored'
  allocated_array(:) = 'ignored'
  if (len(value) /= 7 .or. len(allocated_array) /= 0) stop 17
  deallocate(value,allocated_array)
  print '(a)', 'character length parameters passed'
end program
