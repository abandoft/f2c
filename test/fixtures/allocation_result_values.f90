module allocation_result_values_cases
  implicit none
  type :: item
    integer, allocatable :: values(:)
    character(len=:), allocatable :: text
  end type
contains
  function byte_value() result(value)
    integer(kind=1), allocatable :: value
    allocate(value)
    value = -73_1
  end function
  function wide_value() result(value)
    integer(kind=8), allocatable :: value
    allocate(value)
    value = 1234567890123_8
  end function
  function real_value() result(value)
    real(kind=8), allocatable :: value
    allocate(value)
    value = 0.125d0
  end function
  function complex_value() result(value)
    complex(kind=8), allocatable :: value
    allocate(value)
    value = cmplx(3.0d0, -2.0d0, kind=8)
  end function
  function logical_value() result(value)
    logical, allocatable :: value
    allocate(value)
    value = .true.
  end function
  function word(n) result(value)
    integer,intent(in) :: n
    character(len=:),allocatable :: value
    value = repeat('q', n)
  end function
  function object() result(value)
    type(item),allocatable :: value
    allocate(value)
    allocate(value%values, source=[2,4,6])
    value%text = 'a' // achar(0) // 'b'
  end function
end module
program allocation_result_values
  use allocation_result_values_cases
  implicit none
  integer(kind=1),allocatable :: bytes(:)
  integer(kind=8),allocatable :: wide
  real(kind=8),allocatable :: real_scalar
  complex(kind=8),allocatable :: complex_scalar
  logical,allocatable :: logical_array(:)
  character(len=:),allocatable :: text, empty, molded
  type(item),allocatable :: first, second
  allocate(bytes(3), source=byte_value())
  if (any(bytes /= -73_1)) stop 1
  allocate(wide, source=wide_value())
  if (wide /= 1234567890123_8) stop 2
  allocate(real_scalar, source=real_value())
  if (abs(real_scalar - 0.125d0) > epsilon(real_scalar)) stop 3
  allocate(complex_scalar, source=complex_value())
  if (abs(complex_scalar - cmplx(3.0d0,-2.0d0,kind=8)) > epsilon(real_scalar)) stop 4
  allocate(logical_array(4), source=logical_value())
  if (.not.all(logical_array)) stop 5
  allocate(text, source=word(2) // achar(0) // word(3))
  if (len(text) /= 6 .or. text /= 'qq' // achar(0) // 'qqq') stop 6
  allocate(empty, source=word(0))
  if (len(empty) /= 0) stop 7
  allocate(molded, mold=word(5))
  if (len(molded) /= 5) stop 8
  allocate(first, second, source=object())
  if (any(first%values /= [2,4,6]) .or. first%text /= 'a' // achar(0) // 'b') stop 9
  first%values(1) = 99
  if (second%values(1) /= 2 .or. second%text /= 'a' // achar(0) // 'b') stop 10
  deallocate(bytes, wide, real_scalar, complex_scalar, logical_array, text, empty, molded, first, second)
  print '(A)', 'allocation result values passed'
end program
