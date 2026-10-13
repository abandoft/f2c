module readonly_derived_call_model
  implicit none
  type :: item
    integer, allocatable :: payload(:)
  contains
    procedure, nopass :: measure => score
  end type
  type(item), pointer :: observed => null()
  integer :: observations = 0
contains
  subroutine observe(value)
    type(item), target :: value
    if (value%payload(1) /= 17) stop 1
    observed => value
    observations = 1
  end subroutine

  subroutine forward(value)
    type(item), intent(in), target :: value
    procedure(score), pointer :: operation
    procedure(observe), pointer :: visitor
    integer :: result
    call observe(value=value)
    result = score(value)
    if (result /= 17) stop 2
    result = value%measure(value)
    if (result /= 17) stop 3
    operation => score
    result = operation(value)
    if (result /= 17) stop 4
    operation => score_plus
    result = operation(value)
    if (result /= 18) stop 9
    call observe_internal(value)
    visitor => observe_again
    call visitor(value)
    if (observations /= 2) stop 10
  contains
    subroutine observe_internal(argument)
      type(item), target :: argument
      call observe(argument)
      if (argument%payload(1) /= value%payload(1)) stop 5
    end subroutine
  end subroutine

  integer function score(value)
    type(item) :: value
    score = value%payload(1)
  end function
  integer function score_plus(value)
    type(item) :: value
    score_plus = value%payload(1) + 1
  end function
  subroutine observe_again(value)
    type(item), target :: value
    call observe(value)
    observations = 2
  end subroutine
  subroutine apply(operation,value)
    procedure(score) :: operation
    type(item), intent(in) :: value
    integer :: result
    result = operation(value)
    if (result /= 18) stop 11
  end subroutine
end module

program readonly_derived_calls
  use readonly_derived_call_model, only: item, observed, forward, apply, score_plus, read_score => score
  implicit none
  type(item), target :: value
  procedure(read_score), pointer :: reader
  allocate(value%payload(1))
  value%payload = 17
  call forward(value)
  if (.not. associated(observed,value)) stop 6
  if (value%payload(1) /= 17) stop 7
  reader => read_score
  if (reader(value) /= 17) stop 8
  call apply(score_plus,value)
  print '(A)', 'readonly derived procedure calls preserve borrowed identity'
end program
