module result_snapshot_cases
  implicit none
  integer :: final_calls = 0
  type :: leaf
    integer, allocatable :: payload(:)
  contains
    final :: finish_leaf
  end type
  type :: item
    type(leaf), allocatable :: child
    character(len=:), allocatable :: text
  contains
    final :: finish_item
  end type
  type(item), target :: storage
contains
  subroutine finish_leaf(value)
    type(leaf), intent(inout) :: value
    final_calls = final_calls + 100
    if (allocated(value%payload)) value%payload = -1
  end subroutine
  subroutine finish_item(value)
    type(item), intent(inout) :: value
    final_calls = final_calls + 1
    if (allocated(value%text)) value%text = 'final'
  end subroutine
  function selected() result(value)
    type(item), pointer :: value
    value => storage
  end function
  integer function consume(value) result(answer)
    type(item), intent(in) :: value
    answer = sum(value%child%payload)
  end function
end module
program result_snapshot
  use result_snapshot_cases
  implicit none
  type(item) :: copy
  integer :: before, answer
  allocate(storage%child)
  allocate(storage%child%payload(2))
  storage%child%payload = [11,22]
  storage%text = 'original'
  before = final_calls
  copy = selected()
  if (final_calls /= before + 1) stop 1
  if (any(storage%child%payload /= [11,22]) .or. storage%text /= 'original') stop 2
  copy%child%payload(1) = 99
  if (storage%child%payload(1) /= 11) stop 3
  before = final_calls
  answer = consume(selected())
  if (answer /= 33 .or. final_calls /= before) stop 4
  copy = selected()
  if (final_calls /= before + 101) stop 5
  if (any(copy%child%payload /= [11,22]) .or. copy%text /= 'original') stop 6
  print *, 'borrowed result snapshot finalization preserved'
end program
