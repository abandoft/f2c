program parenthesized_intrinsic_values
  implicit none
  integer :: scalar, grid(-1:0,4:5), empty(0), calls
  real(kind=8) :: number
  complex(kind=8) :: pair
  logical :: flag
  integer, parameter :: constant = ((2 + (3)))
  real(kind=8), parameter :: real_constant = ((1.25_8))
  complex(kind=8), parameter :: complex_constant = (((1.0_8,2.0_8)))
  character(len=4), parameter :: text_constant = (('ab' // 'cd'))
  integer :: initialized(2) = (([3,4]))
  character(len=2) :: initialized_text(2) = ((['ab','cd']))
  if (constant /= 5 .or. real_constant /= 1.25_8) stop 1
  if (complex_constant /= (1.0_8,2.0_8) .or. text_constant /= 'abcd') stop 2
  if (any(initialized /= [3,4]) .or. any(initialized_text /= ['ab','cd'])) stop 3
  scalar = 7
  number = 2.5_8
  pair = (3.0_8,4.0_8)
  flag = .true.
  call check_scalars((scalar), (number), (pair), ((flag)))
  grid = reshape([1,2,3,4],[2,2])
  if (any(lbound((grid)) /= [1,1])) stop 4
  if (any(ubound(((grid))) /= [2,2])) stop 5
  if (any(shape((grid)) /= [2,2]) .or. sum((grid)) /= 10) stop 6
  call check_grid(((grid)))
  grid = reshape([1,2,3,4],[2,2])
  call check_reverse((grid(0:-1:-1,5:4:-1)))
  call check_empty((empty))
  calls = 0
  call check_function((make_value()))
  if (calls /= 1) stop 7
  print '(A)', 'parenthesized intrinsic contracts passed'
contains
  subroutine check_scalars(a,b,c,d)
    integer, intent(in) :: a
    real(kind=8), intent(in) :: b
    complex(kind=8), intent(in) :: c
    logical, intent(in) :: d
    scalar = 70
    number = 25.0_8
    pair = (30.0_8,40.0_8)
    flag = .false.
    if (a /= 7 .or. b /= 2.5_8 .or. c /= (3.0_8,4.0_8) .or. .not. d) stop 10
  end subroutine
  subroutine check_grid(value)
    integer, intent(in) :: value(:,:)
    grid = 9
    if (any(value /= reshape([1,2,3,4],[2,2]))) stop 11
  end subroutine
  subroutine check_reverse(value)
    integer, intent(in) :: value(2,2)
    grid = 8
    if (any(value /= reshape([4,3,2,1],[2,2]))) stop 12
  end subroutine
  subroutine check_empty(value)
    integer, intent(in) :: value(:)
    if (size(value) /= 0 .or. lbound(value,1) /= 1 .or. ubound(value,1) /= 0) stop 13
  end subroutine
  integer function make_value()
    calls = calls + 1
    make_value = 42
  end function
  subroutine check_function(value)
    integer, intent(in) :: value
    if (value /= 42) stop 14
  end subroutine
end program
