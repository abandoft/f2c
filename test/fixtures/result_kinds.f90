module result_kind_cases
  implicit none
contains
  function integer_one() result(value)
    integer(kind=1), allocatable :: value
    allocate(value)
    value = -120_1
  end function
  function integer_two() result(value)
    integer(kind=2), allocatable :: value
    allocate(value)
    value = -32000_2
  end function
  function integer_eight() result(value)
    integer(kind=8), allocatable :: value
    allocate(value)
    value = 10000000000_8
  end function
  function real_four() result(value)
    real(kind=4), allocatable :: value
    allocate(value)
    value = 1.5
  end function
  function real_eight() result(value)
    real(kind=8), allocatable :: value
    allocate(value)
    value = 2.5d0
  end function
  function complex_four() result(value)
    complex(kind=4), allocatable :: value
    allocate(value)
    value = cmplx(1.0,2.0)
  end function
  function complex_eight() result(value)
    complex(kind=8), allocatable :: value
    allocate(value)
    value = cmplx(3.0d0,4.0d0,kind=8)
  end function
  function logical_four() result(value)
    logical(kind=4), allocatable :: value
    allocate(value)
    value = .true.
  end function
end module
program result_kinds
  use result_kind_cases
  implicit none
  integer :: iteration
  do iteration = 1, 100
    if (integer_one() /= -120_1) stop 1
    if (integer_two() /= -32000_2) stop 2
    if (integer_eight() /= 10000000000_8) stop 3
    if (abs(real_four() - 1.5) > 0.001) stop 4
    if (abs(real_eight() - 2.5d0) > 0.001d0) stop 5
    if (.not.(abs(real(complex_four()) - 1.0) <= 0.0) .or. &
        .not.(abs(aimag(complex_four()) - 2.0) <= 0.0)) stop 6
    if (.not.(abs(real(complex_eight(),kind=8) - 3.0d0) <= 0.0d0) .or. &
        .not.(abs(aimag(complex_eight()) - 4.0d0) <= 0.0d0)) stop 7
    if (.not. logical_four()) stop 8
  end do
  print *, 'scalar result kind matrix passed'
end program
