program derived_pointer_component
  implicit none
  type :: leaf
    integer :: value
  end type leaf
  type :: node
    integer, pointer :: value
    integer, pointer :: values(:)
    integer, pointer :: matrix(:, :)
    character(len=:), pointer :: words(:)
    type(leaf), pointer :: children(:)
  end type node
  type(node) :: item, source
  integer, target :: storage(-2:9)
  integer, target :: grid(2, 3)
  character(len=3), target :: word_storage(-1:2)
  type(leaf), target :: leaf_storage(2)
  integer :: i, j

  storage = [(i, i = -2, 9)]
  do j = 1, 3
    do i = 1, 2
      grid(i, j) = i + 2 * (j - 1)
    end do
  end do
  item%value => storage(0)
  if (.not. associated(item%value)) stop 1
  if (.not. associated(item%value, storage(0))) stop 2
  item%value = 19
  if (storage(0) /= 19) stop 3
  nullify(item%value)
  if (associated(item%value)) stop 4

  item%values(4:) => storage(0:6:2)
  if (lbound(item%values, 1) /= 4 .or. ubound(item%values, 1) /= 7) stop 5
  if (.not. associated(item%values, storage(0:6:2))) stop 6
  item%values(5) = 31
  if (storage(2) /= 31) stop 7
  call check_values(item%values)
  call clear_values(item%values)
  if (associated(item%values)) stop 25

  source%values(-1:) => storage(-1:4)
  item%values => source%values(0:3:2)
  if (.not. associated(item%values, source%values(0:3:2))) stop 8
  if (sum(item%values) /= storage(0) + storage(2)) stop 9

  item%matrix(0:1, -1:1) => grid
  if (lbound(item%matrix, 1) /= 0 .or. ubound(item%matrix, 1) /= 1) stop 10
  if (lbound(item%matrix, 2) /= -1 .or. ubound(item%matrix, 2) /= 1) stop 11
  do j = -1, 1
    do i = 0, 1
      if (item%matrix(i, j) /= grid(i + 1, j + 2)) stop 12
    end do
  end do
  item%matrix(:, 0) = next_integer()
  if (any(grid(:, 2) /= 9)) stop 13
  nullify(item%matrix)
  if (associated(item%matrix)) stop 14

  allocate(item%values(-1:2))
  item%values = [3, 5, 7, 11]
  item%values(0:2) = item%values(-1:1)
  if (any(item%values /= [3, 3, 5, 7])) stop 15
  deallocate(item%values)
  if (associated(item%values)) stop 16

  word_storage = ['RED', 'BLU', 'ONE', 'TWO']
  item%words(0:) => word_storage
  if (len(item%words) /= 3 .or. lbound(item%words, 1) /= 0) stop 19
  item%words(1:2) = ['YES', 'NO ']
  if (word_storage(0) /= 'YES' .or. word_storage(1) /= 'NO ') stop 20
  item%words = next_word()
  if (any(item%words /= 'ALL')) stop 26
  if (.not. associated(item%words, word_storage)) stop 21
  nullify(item%words)
  if (associated(item%words)) stop 22

  leaf_storage(1)%value = 1
  leaf_storage(2)%value = 2
  item%children => leaf_storage
  item%children = make_leaf(9)
  if (leaf_storage(1)%value /= 9 .or. leaf_storage(2)%value /= 9) stop 23
  if (.not. associated(item%children, leaf_storage)) stop 24
  nullify(item%children)

contains

  subroutine check_values(values)
    integer, intent(in) :: values(:)
    if (lbound(values, 1) /= 1 .or. size(values) /= 4) stop 17
    if (values(2) /= 31) stop 18
  end subroutine check_values

  subroutine clear_values(values)
    integer, pointer, intent(inout) :: values(:)
    nullify(values)
  end subroutine clear_values

  integer function next_integer()
    integer, save :: calls = 0
    calls = calls + 1
    next_integer = 8 + calls
  end function next_integer

  function next_word() result(value)
    character(len=3) :: value
    integer, save :: calls = 0
    calls = calls + 1
    value = merge('ALL', 'BAD', calls == 1)
  end function next_word

  function make_leaf(number) result(value)
    integer, intent(in) :: number
    type(leaf) :: value
    integer, save :: calls = 0
    calls = calls + 1
    value%value = number + calls - 1
  end function make_leaf
end program derived_pointer_component
