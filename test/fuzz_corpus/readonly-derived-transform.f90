module readonly_derived_model
  implicit none
  type :: value
    integer, allocatable :: payload(:)
  end type
contains
  subroutine inspect(source)
    type(value), intent(in) :: source(:)
    type(value), allocatable :: output(:)
    allocate(output(0))
    output = reshape((source(size(source):1:-1)), [size(source)])
    output = cshift(source(1:0), 1)
  end subroutine
end module
program readonly_derived
  use readonly_derived_model
  implicit none
  type(value) :: source(2)
  allocate(source(1)%payload(1), source(2)%payload(1))
  source(1)%payload = 1
  source(2)%payload = 2
  call inspect(source)
end program
