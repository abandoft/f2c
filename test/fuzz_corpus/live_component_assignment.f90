0module component_seed
  type :: box
    character(:), allocatable :: text
  end type
  integer :: calls = 0
contains
  integer function index_once() result(index)
    calls = calls + 1
    index = 2
  end function
end module
program component_main
  use component_seed
  type(box) :: objects(2)
  objects(index_once())%text = 'once'
  objects(2)%text = objects(2)%text // '!'
  if (calls /= 1 .or. objects(2)%text /= 'once!') stop 1
  deallocate(objects(2)%text)
end program
