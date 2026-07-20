module pointer_section_bounds
  implicit none
  integer :: evaluations = 0
  integer, target :: module_storage(6)
  integer, pointer :: module_view(:)
  integer, pointer, contiguous :: module_contiguous_view(:)
  type :: contiguous_worker
  contains
    procedure :: mutate => mutate_bound
  end type contiguous_worker
contains
  integer function bound(value)
    integer, intent(in) :: value
    evaluations = evaluations + 1
    bound = value
  end function bound

  subroutine associate_module()
    module_view => module_storage(2:6:2)
    module_contiguous_view => module_storage
  end subroutine associate_module

  integer function mutate_bound(self, pointer_value)
    class(contiguous_worker), intent(in) :: self
    integer, contiguous, intent(inout) :: pointer_value(:)
    pointer_value(3) = 1404
    mutate_bound = sum(pointer_value)
  end function mutate_bound
end module pointer_section_bounds

program pointer_section
  use pointer_section_bounds, only: associate_module, bound, evaluations, &
                                    contiguous_worker, module_contiguous_view, module_storage, &
                                    module_view
  implicit none
  integer, target :: storage(-2:9)
  integer, target :: matrix(2:4, -1:3)
  integer, pointer :: values(:)
  integer, pointer :: nested(:)
  integer, pointer :: remapped(:, :)
  integer, pointer :: matrix_map(:, :)
  integer, pointer :: strided_map(:, :)
  integer, pointer, contiguous :: contiguous_view(:)
  integer, pointer :: element
  character(len=4), target :: words(4)
  character(len=:), pointer :: selected_words(:)
  integer :: i
  integer :: j
  type(contiguous_worker) :: worker

  evaluations = 0
  module_storage = [1, 2, 3, 4, 5, 6]
  call associate_module()
  if (size(module_view) /= 3) stop 19
  module_view(2) = 404
  if (module_storage(4) /= 404) stop 20
  if (.not. associated(module_contiguous_view, module_storage)) stop 47
  words = ['zero', 'one ', 'two ', 'last']
  selected_words => words(1:4:2)
  if (len(selected_words) /= 4 .or. size(selected_words) /= 2) stop 21
  if (.not. associated(selected_words, words(1:4:2))) stop 40
  if (associated(selected_words, words(2:4:2))) stop 41
  selected_words(2) = 'done'
  if (words(3) /= 'done') stop 22
  do i = -2, 9
    storage(i) = 100 + i
  end do
  do j = -1, 3
    do i = 2, 4
      matrix(i, j) = 1000 * i + j
    end do
  end do

  remapped(bound(0):bound(2), bound(-1):bound(2)) => storage
  if (evaluations /= 4) stop 24
  if (lbound(remapped, 1) /= 0 .or. ubound(remapped, 1) /= 2) stop 25
  if (lbound(remapped, 2) /= -1 .or. ubound(remapped, 2) /= 2) stop 26
  if (remapped(2, 0) /= storage(3)) stop 27
  remapped(2, 0) = 803
  if (storage(3) /= 803) stop 28

  matrix_map(1:3, -2:1) => matrix
  if (matrix_map(3, 1) /= matrix(4, 2)) stop 29
  if (.not. associated(matrix_map, matrix(:, -1:2))) stop 34
  if (associated(matrix_map, matrix)) stop 35
  matrix_map(3, 1) = 9402
  if (matrix(4, 2) /= 9402) stop 30

  evaluations = 0
  values(-4:) => storage(bound(0):bound(8):bound(2))
  if (evaluations /= 3) stop 1
  if (lbound(values, 1) /= -4 .or. ubound(values, 1) /= 0) stop 2
  if (any(values /= [100, 102, 104, 106, 108])) stop 3
  evaluations = 0
  if (.not. associated(pointer=values, target=storage(bound(0):bound(8):bound(2)))) stop 36
  if (evaluations /= 3) stop 37
  if (associated(values, storage(0:6:2))) stop 38
  if (associated(values, storage(0:8))) stop 39
  values(-2) = 704
  if (storage(4) /= 704) stop 4

  nested => values(-3:0:2)
  if (size(nested) /= 2) stop 5
  nested(2) = 908
  if (storage(6) /= 908) stop 6

  values => storage(8:0:-2)
  if (any(values /= [108, 908, 704, 102, 100])) stop 7
  values(4) = 802
  if (storage(2) /= 802) stop 8

  values => storage(::-1)
  if (size(values) /= 0) stop 9
  if (associated(values, storage(::-1))) stop 49

  values => storage(9:-2:-1)
  if (size(values) /= 12) stop 10
  if (values(1) /= storage(9) .or. values(12) /= storage(-2)) stop 10

  element => storage(3)
  element = 603
  if (storage(3) /= 603) stop 11

  values => matrix(3, -1:3:2)
  if (size(values) /= 3) stop 12
  if (any(values /= [2999, 3001, 3003])) stop 13
  values(2) = 9001
  if (matrix(3, 1) /= 9001) stop 14

  strided_map(1:2, 1:2) => storage(0:6:2)
  if (strided_map(2, 2) /= storage(6)) stop 31
  strided_map(1, 2) = 9904
  if (storage(4) /= 9904) stop 32

  contiguous_view => storage
  if (.not. associated(contiguous_view, storage)) stop 42
  call mutate_contiguous(storage(0:4:2))
  if (storage(2) /= 1202) stop 43
  i = mutate_contiguous_function(storage(0:4:2))
  if (i /= 2606 .or. storage(4) /= 1304) stop 45
  i = worker%mutate(storage(0:4:2))
  if (i /= 2706 .or. storage(4) /= 1404) stop 48
  i = mutate_contiguous_words(words(1:4:2))
  if (i /= 8 .or. words(3) /= 'char') stop 46

  call associate_dummy(nested, storage)
  if (size(nested) /= 4) stop 15
  nested(3) = 777
  if (storage(3) /= 777) stop 16

  call clear_dummy(nested)
  if (associated(nested)) stop 17

  nullify(values, nested, remapped, matrix_map, strided_map, contiguous_view, element, &
          selected_words)
  if (associated(values) .or. associated(nested) .or. associated(element)) stop 18
  if (associated(remapped) .or. associated(matrix_map) .or. associated(strided_map)) stop 33
  if (associated(contiguous_view)) stop 44
  if (associated(selected_words)) stop 23
  print '(a)', 'pointer sections ok'

contains

  subroutine associate_dummy(pointer_value, target_value)
    integer, pointer, intent(out) :: pointer_value(:)
    integer, target, intent(inout) :: target_value(:)
    pointer_value => target_value(2:8:2)
  end subroutine associate_dummy

  subroutine clear_dummy(pointer_value)
    integer, pointer, intent(out) :: pointer_value(:)
    nullify(pointer_value)
  end subroutine clear_dummy

  subroutine mutate_contiguous(pointer_value)
    integer, contiguous, intent(inout) :: pointer_value(:)
    pointer_value(2) = 1202
  end subroutine mutate_contiguous

  integer function mutate_contiguous_function(pointer_value)
    integer, contiguous, intent(inout) :: pointer_value(:)
    pointer_value(3) = 1304
    mutate_contiguous_function = sum(pointer_value)
  end function mutate_contiguous_function

  integer function mutate_contiguous_words(pointer_value)
    character(len=4), contiguous, intent(inout) :: pointer_value(:)
    pointer_value(2) = 'char'
    mutate_contiguous_words = len(pointer_value) * size(pointer_value)
  end function mutate_contiguous_words
end program pointer_section
