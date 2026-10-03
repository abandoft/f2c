0
module results
integer,target :: backing=7
contains
function selected() result(output)
integer,pointer :: output
output=>backing
end function
end module
program main
use results
integer,pointer :: alias
alias=>selected()
end program
