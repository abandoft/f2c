0
module results
type :: item
integer,allocatable :: payload(:)
end type
contains
function value() result(output)
type(item),allocatable :: output
allocate(output)
allocate(output%payload(2))
output%payload=[1,2]
end function
end module
program main
use results
type(item) :: output
output=value()
end program
