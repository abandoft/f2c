0
module results
contains
function value() result(output)
integer,allocatable :: output
allocate(output)
output=7
end function
end module
program main
use results
integer :: output
output=value()+value()
end program
