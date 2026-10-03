module qualified_host_access
  implicit none
contains
  subroutine enclosed(values)
    integer, volatile :: values(:)
    call inner_update()
  contains
    subroutine inner_update()
      values = values + 3
    end subroutine inner_update
  end subroutine enclosed
end module qualified_host_access

program qualified_descriptors
  use qualified_host_access, only: enclosed
  implicit none
  integer, volatile :: observed(6)
  logical, volatile :: selected(6)
  real(kind=8), volatile :: samples(6)
  complex, volatile :: pairs(3)
  character(len=3), volatile :: labels(3)
  integer, asynchronous :: pending(6)
  integer, volatile :: matrix(4,3), empty(0)
  logical(kind=1), volatile :: narrow(3)
  logical(kind=8), volatile :: wide(3)
  character(len=0), volatile :: blank(2)
  integer :: immutable(4)

  observed = [1,2,3,4,5,6]
  selected = [.true.,.false.,.true.,.false.,.true.,.false.]
  samples = [1.0_8,2.0_8,3.0_8,4.0_8,5.0_8,6.0_8]
  pairs = [(1.0,2.0),(3.0,4.0),(5.0,6.0)]
  labels = ['one','two','six']
  pending = observed
  call update(observed(6:1:-2), selected(6:1:-2))
  if (any(observed /= [1,12,3,14,5,16])) stop 1
  if (.not. all(selected)) stop 2
  call reduce(samples(6:1:-2), pairs(3:1:-1))
  call characters(labels(3:1:-1))
  if (any(labels /= ['one','new','six'])) stop 3
  if (character_copy(labels(3:1:-1)) /= 1) stop 21
  if (any(labels /= ['one','cpy','six'])) stop 22
  call asynchronous_update(pending(1:6:2))
  if (any(pending /= [2,2,4,4,6,6])) stop 4
  matrix = reshape([1,2,3,4,5,6,7,8,9,10,11,12],[4,3])
  call matrix_update(matrix(4:1:-2,3:1:-1))
  if (any(matrix /= reshape([1,102,3,104,5,106,7,108,9,110,11,112],[4,3]))) stop 13
  call empty_update(empty)
  wide = [.true.,.false.,.true.]
  call logical_conversion(narrow(3:1:-1),wide(3:1:-1))
  if (any(narrow .neqv. wide)) stop 14
  call zero_length(blank(2:1:-1))
  immutable = [1,2,3,4]
  call readonly_forward(immutable(4:1:-1))
  if (qualified_sum(observed(6:1:-2)) /= 42) stop 18
  call enclosed(observed(1:6:2))
  if (any(observed /= [4,12,6,14,8,16])) stop 19
  print '(A)', 'qualified descriptor contracts passed'
contains
  subroutine update(values, flags)
    integer, volatile :: values(:)
    logical, volatile :: flags(:)
    values = values + 10
    flags = .true.
    call forward(values)
  end subroutine update

  subroutine forward(values)
    integer, volatile, optional :: values(:)
    if (.not. present(values)) stop 5
    if (sum(values) /= 42) stop 6
    if (values(1) /= 16 .or. values(3) /= 12) stop 7
  end subroutine forward

  subroutine reduce(values, numbers)
    real(kind=8), volatile :: values(:)
    complex, volatile :: numbers(:)
    if (sum(values) /= 12.0_8) stop 8
    if (sum(numbers) /= (9.0,12.0)) stop 9
    if (dot_product(numbers,numbers) /= (91.0,0.0)) stop 10
  end subroutine reduce

  subroutine characters(values)
    character(len=*), volatile :: values(:)
    if (values(1) /= 'six' .or. values(3) /= 'one') stop 11
    if (count(values == 'six') /= 1) stop 12
    values(2) = 'new'
  end subroutine characters

  subroutine asynchronous_update(values)
    integer, asynchronous :: values(:)
    values = values + 1
  end subroutine asynchronous_update

  subroutine matrix_update(values)
    integer, volatile :: values(:,:)
    integer :: snapshot(2,3)
    snapshot = values
    if (any(snapshot /= reshape([12,10,8,6,4,2],[2,3]))) stop 15
    values = snapshot + 100
  end subroutine matrix_update


  subroutine empty_update(values)
    integer, volatile :: values(:)
    values = values + 1
    values = 0
    if (size(values) /= 0) stop 16
  end subroutine empty_update

  subroutine logical_conversion(target, source)
    logical(kind=1), volatile :: target(:)
    logical(kind=8), volatile :: source(:)
    target = source
  end subroutine logical_conversion

  subroutine zero_length(values)
    character(len=*), volatile :: values(:)
    values = 'ignored'
    if (size(values) /= 2 .or. len(values) /= 0) stop 17
  end subroutine zero_length

  subroutine readonly_forward(values)
    integer, intent(in) :: values(:)
    call readonly_sink(values)
  end subroutine readonly_forward

  subroutine readonly_sink(values)
    integer, intent(in) :: values(:)
    if (sum(values) /= 10 .or. values(1) /= 4) stop 20
  end subroutine readonly_sink

  function qualified_sum(values) result(total)
    integer, volatile :: values(:)
    integer :: total
    total = sum(values)
  end function qualified_sum

  function character_copy(values) result(matches)
    character(len=*) :: values(3)
    integer :: matches
    if (values(1) /= 'six' .or. values(3) /= 'one') stop 23
    values(2) = 'cpy'
    matches = count(values == 'cpy')
  end function character_copy

end program qualified_descriptors
