module pointer_bound_counter
  implicit none
  integer :: evaluations = 0
contains
  integer function bound(value)
    integer, intent(in) :: value
    evaluations = evaluations + 1
    bound = value
  end function bound
end module pointer_bound_counter

program pointer_bounds_once
  use pointer_bound_counter, only: bound, evaluations
  implicit none
  integer, target :: storage(6)
  integer, pointer :: view(:)
  integer, pointer :: matrix(:, :)

  storage = [10, 20, 30, 40, 50, 60]

  evaluations = 0
  view(bound(-2):) => storage
  if (evaluations /= 1) stop 1
  if (lbound(view, 1) /= -2 .or. ubound(view, 1) /= 3) stop 2
  view(-1) = 200
  if (storage(2) /= 200) stop 3

  evaluations = 0
  matrix(bound(0):bound(1), bound(-1):bound(1)) => storage
  if (evaluations /= 4) stop 4
  if (matrix(1, 1) /= storage(6)) stop 5
  matrix(0, 0) = 400
  if (storage(3) /= 400) stop 6

  print '(a)', 'pointer bounds once ok'
end program pointer_bounds_once
