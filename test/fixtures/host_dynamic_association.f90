program host_dynamic_association
  implicit none(type, external)
  external :: verify_saved_host

  type :: box
    integer :: value
  end type box

  type :: cell
    integer, allocatable :: payload(:)
  end type cell

  integer, allocatable :: values(:), left(:), right(:)
  integer, allocatable :: function_values(:), produced(:), released(:)
  integer, target :: first_target(2), second_target(3)
  integer, pointer :: view(:), reverse_view(:)
  character(:), allocatable :: text
  character(len=4) :: word
  character(len=8) :: doubled
  type(box) :: item
  type(cell), allocatable :: cells(:)
  integer :: total

  allocate(values(1), left(1), right(1), function_values(1), released(1), cells(1))
  allocate(cells(1)%payload(1))
  values = 1
  left = 2
  right = 3
  function_values = 4
  released = 5
  cells(1)%payload = 6
  first_target = [1, 2]
  second_target = [7, 8, 9]
  view => first_target
  reverse_view => first_target
  text = 'old'

  call resize_values()
  call descend(2)
  call switch_target()
  word = refresh_text()
  doubled = refresh_text() // refresh_text()
  word = choose_words(refresh_text(), refresh_text())
  total = combine(refresh_numbers(), refresh_numbers())
  total = max(refresh_numbers(), refresh_numbers()) * 2
  item = build_box()
  produced = build_output()
  call rebuild_cells()
  call release_storage()
  call verify_saved_host(1)
  call verify_saved_host(2)

  if (lbound(values, 1) /= 1 .or. ubound(values, 1) /= 2) error stop 1
  if (any(values /= [8, 9])) error stop 2
  if (any(left /= [10, 11])) error stop 3
  if (any(right /= [30, 31])) error stop 4
  if (.not. associated(view, second_target)) error stop 5
  if (any(view /= [7, 8, 9])) error stop 6
  if (word /= 'done' .or. text /= 'dynamic') error stop 7
  if (total /= 18) error stop 8
  if (lbound(function_values, 1) /= -1 .or. ubound(function_values, 1) /= 1) error stop 9
  if (any(function_values /= [2, 3, 4])) error stop 10
  if (item%value /= 17) error stop 11
  if (any(produced /= [40, 41, 42])) error stop 12
  if (allocated(released)) error stop 13
  if (size(cells) /= 2) error stop 14
  if (any(cells(1)%payload /= [50, 51])) error stop 15
  if (any(cells(2)%payload /= [60, 61, 62])) error stop 16
  if (lbound(reverse_view, 1) /= 1 .or. ubound(reverse_view, 1) /= 3) error stop 17
  if (any(reverse_view /= [9, 8, 7])) error stop 18
  if (doubled /= 'donedone') error stop 24

  write (*, '(I0,1X,I0,1X,A,1X,A,1X,3(I0,1X))') &
      total, item%value, word, text, produced

contains

  subroutine resize_values()
    deallocate(values)
    allocate(values(-2:0))
    values = [4, 5, 6]
    if (sum(values) == 15) return
    error stop 19
  end subroutine resize_values

  recursive subroutine descend(level)
    integer, intent(in) :: level
    if (level == 0) then
      deallocate(left, right)
      allocate(left(2), right(3))
      left = [10, 11]
      right = [20, 21, 22]
    else
      call forward(level - 1)
    end if
  end subroutine descend

  subroutine forward(level)
    integer, intent(in) :: level
    call descend(level)
  end subroutine forward

  subroutine switch_target()
    view => second_target
    reverse_view => second_target(3:1:-1)
  end subroutine switch_target

  character(len=4) function refresh_text() result(answer)
    deallocate(text)
    allocate(character(len=7) :: text)
    text = 'dynamic'
    answer = 'done'
  end function refresh_text

  character(len=4) function choose_words(first, second) result(answer)
    character(len=*), intent(in) :: first, second
    if (first /= second) error stop 23
    answer = first
  end function choose_words

  integer function refresh_numbers() result(answer)
    deallocate(function_values)
    allocate(function_values(-1:1))
    function_values = [2, 3, 4]
    answer = sum(function_values)
    if (answer == 9) return
    error stop 20
  end function refresh_numbers

  integer function combine(first, second) result(answer)
    integer, intent(in) :: first, second
    answer = first + second
  end function combine

  function build_box() result(answer)
    type(box) :: answer
    deallocate(values)
    allocate(values(2))
    values = [8, 9]
    answer%value = sum(values)
  end function build_box

  function build_output() result(answer)
    integer, allocatable :: answer(:)
    deallocate(right)
    allocate(right(2))
    right = [30, 31]
    allocate(answer(3))
    answer = [40, 41, 42]
  end function build_output

  subroutine release_storage()
    deallocate(released)
  end subroutine release_storage

  subroutine rebuild_cells()
    deallocate(cells)
    allocate(cells(2))
    allocate(cells(1)%payload(2), cells(2)%payload(3))
    cells(1)%payload = [50, 51]
    cells(2)%payload = [60, 61, 62]
  end subroutine rebuild_cells

end program host_dynamic_association

subroutine verify_saved_host(iteration)
  implicit none(type, external)
  integer, intent(in) :: iteration
  integer, allocatable, save :: saved_values(:)

  if (.not. allocated(saved_values)) then
    allocate(saved_values(1))
    saved_values = 1
  end if
  call update_saved()
  if (size(saved_values) /= iteration + 1) error stop 21
  if (any(saved_values /= iteration * 10)) error stop 22

contains

  subroutine update_saved()
    integer :: next_size

    next_size = size(saved_values) + 1
    deallocate(saved_values)
    allocate(saved_values(next_size))
    saved_values = iteration * 10
  end subroutine update_saved

end subroutine verify_saved_host
