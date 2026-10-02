program parenthesized_values
  implicit none
  type :: item
    integer :: value = 0
    integer, allocatable :: payload(:)
    integer, pointer :: link => null()
  end type item
  integer :: scalar, grid(-1:0,4:5), empty(0), evaluations, index
  integer, target :: linked
  logical :: flag
  real(kind=8) :: number
  complex(kind=8) :: pair
  type(item) :: record, records(2)
  integer, parameter :: constant = ((2 + (3)))
  real(kind=8), parameter :: real_constant = ((1.25_8))
  complex(kind=8), parameter :: complex_constant = (((1.0_8,2.0_8)))
  character(len=4), parameter :: text_constant = (('ab' // 'cd'))

  if (constant /= 5 .or. real_constant /= 1.25_8) stop 1
  if (complex_constant /= (1.0_8,2.0_8) .or. text_constant /= 'abcd') stop 2
  scalar = 7
  flag = .true.
  number = 2.5_8
  pair = (3.0_8,4.0_8)
  call check_scalars((scalar), ((flag)), (number), (pair))
  if (scalar /= 70 .or. flag .or. number /= 25.0_8 .or. pair /= (30.0_8,40.0_8)) stop 3

  grid = reshape([1,2,3,4],[2,2])
  if (any(lbound((grid)) /= [1,1])) stop 4
  if (any(ubound(((grid))) /= [2,2])) stop 5
  if (any(shape((grid)) /= [2,2]) .or. sum((grid)) /= 10) stop 6
  call check_grid(((grid)))
  if (any(grid /= 9)) stop 7
  grid = reshape([1,2,3,4],[2,2])
  call check_section((grid(0:-1:-1,5:4:-1)))
  call check_empty((empty))

  linked = 17
  record%value = 11
  allocate(record%payload(2))
  record%payload = [12,13]
  record%link => linked
  call check_record((record))
  if (record%value /= 91 .or. allocated(record%payload)) stop 11
  if (.not. associated(record%link)) stop 12
  do index = 1, 2
    records(index)%value = index
    allocate(records(index)%payload(2))
    records(index)%payload = [index, index+10]
  end do
  call check_records((records))
  if (allocated(records(1)%payload) .or. allocated(records(2)%payload)) stop 13

  evaluations = 0
  call check_function((make_value()))
  if (evaluations /= 1) stop 14
  call check_owned_record((make_record()))
  if (evaluations /= 2) stop 15
  record = ((make_record()))
  if (evaluations /= 3 .or. record%value /= 43) stop 16
  if (.not. allocated(record%payload) .or. record%payload(1) /= 44) stop 17
  record = ((merge((make_record()), (record), .true.)))
  if (evaluations /= 4 .or. record%payload(1) /= 44) stop 18
  deallocate(record%payload)
  print '(A)', 'parenthesized value contracts passed'
contains
  subroutine check_scalars(a,b,c,d)
    integer, intent(in) :: a
    logical, intent(in) :: b
    real(kind=8), intent(in) :: c
    complex(kind=8), intent(in) :: d
    scalar = 70
    flag = .false.
    number = 25.0_8
    pair = (30.0_8,40.0_8)
    if (a /= 7 .or. .not. b .or. c /= 2.5_8 .or. d /= (3.0_8,4.0_8)) stop 20
  end subroutine
  subroutine check_grid(value)
    integer, intent(in) :: value(:,:)
    grid = 9
    if (any(value /= reshape([1,2,3,4],[2,2]))) stop 21
    if (any(lbound(value) /= [1,1])) stop 22
  end subroutine
  subroutine check_section(value)
    integer, intent(in) :: value(2,2)
    grid = 8
    if (any(value /= reshape([4,3,2,1],[2,2]))) stop 23
  end subroutine
  subroutine check_empty(value)
    integer, intent(in) :: value(:)
    if (size(value) /= 0 .or. lbound(value,1) /= 1 .or. ubound(value,1) /= 0) stop 24
  end subroutine
  subroutine check_record(value)
    type(item), intent(in) :: value
    record%value = 91
    deallocate(record%payload)
    if (value%value /= 11 .or. .not. allocated(value%payload)) stop 28
    if (any(value%payload /= [12,13])) stop 29
    if (.not. associated(value%link,linked)) stop 30
  end subroutine
  subroutine check_records(value)
    type(item), intent(in) :: value(:)
    integer :: j
    do j = 1, 2
      records(j)%value = 99
      deallocate(records(j)%payload)
    end do
    do j = 1, 2
      if (value(j)%value /= j .or. .not. allocated(value(j)%payload)) stop 31
      if (any(value(j)%payload /= [j,j+10])) stop 32
    end do
  end subroutine
  integer function make_value()
    evaluations = evaluations + 1
    make_value = 42
  end function
  subroutine check_function(value)
    integer, intent(in) :: value
    if (value /= 42) stop 33
  end subroutine
  function make_record() result(value)
    type(item) :: value
    evaluations = evaluations + 1
    value%value = 43
    allocate(value%payload(1))
    value%payload(1) = 44
  end function
  subroutine check_owned_record(value)
    type(item), intent(in) :: value
    if (value%value /= 43 .or. .not. allocated(value%payload)) stop 34
    if (value%payload(1) /= 44) stop 35
  end subroutine
end program
