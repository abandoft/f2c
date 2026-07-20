module pointer_section_bounds
  implicit none
  integer :: evaluations = 0
  integer, target :: module_storage(6)
  integer, pointer :: module_view(:)
contains
  integer function bound(value)
    integer, intent(in) :: value
    evaluations = evaluations + 1
    bound = value
  end function bound

  subroutine associate_module()
    module_view => module_storage(2:6:2)
  end subroutine associate_module
end module pointer_section_bounds

program pointer_section
  use pointer_section_bounds, only: associate_module, bound, evaluations, &
                                    module_storage, module_view
  implicit none
  integer, target :: storage(-2:9)
  integer, target :: matrix(2:4, -1:3)
  integer, pointer :: values(:)
  integer, pointer :: nested(:)
  integer, pointer :: element
  character(len=4), target :: words(4)
  character(len=:), pointer :: selected_words(:)
  integer :: i
  integer :: j

  evaluations = 0
  module_storage = [1, 2, 3, 4, 5, 6]
  call associate_module()
  if (size(module_view) /= 3) stop 19
  module_view(2) = 404
  if (module_storage(4) /= 404) stop 20
  words = ['zero', 'one ', 'two ', 'last']
  selected_words => words(1:4:2)
  if (len(selected_words) /= 4 .or. size(selected_words) /= 2) stop 21
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

  values => storage(bound(0):bound(8):bound(2))
  if (evaluations /= 3) stop 1
  if (lbound(values, 1) /= 1 .or. ubound(values, 1) /= 5) stop 2
  if (any(values /= [100, 102, 104, 106, 108])) stop 3
  values(3) = 704
  if (storage(4) /= 704) stop 4

  nested => values(2:5:2)
  if (size(nested) /= 2) stop 5
  nested(2) = 908
  if (storage(6) /= 908) stop 6

  values => storage(8:0:-2)
  if (any(values /= [108, 908, 704, 102, 100])) stop 7
  values(4) = 802
  if (storage(2) /= 802) stop 8

  values => storage(::-1)
  if (size(values) /= 0) stop 9

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

  call associate_dummy(nested, storage)
  if (size(nested) /= 4) stop 15
  nested(3) = 777
  if (storage(3) /= 777) stop 16

  call clear_dummy(nested)
  if (associated(nested)) stop 17

  nullify(values, nested, element, selected_words)
  if (associated(values) .or. associated(nested) .or. associated(element)) stop 18
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
end program pointer_section
