module live_component_assignment_model
  implicit none
  integer :: object_evaluations = 0
  type :: object
    character(:), allocatable :: text
  end type object
contains
  integer function next_object_index() result(index)
    object_evaluations = object_evaluations + 1
    index = 2
  end function next_object_index
end module live_component_assignment_model

program live_component_assignment
  use live_component_assignment_model
  implicit none
  type(object) :: returned_objects(2)
  returned_objects(next_object_index())%text = 'once'
  if (object_evaluations /= 1 .or. returned_objects(2)%text /= 'once') stop 49
  deallocate(returned_objects(2)%text)
  print '(a)', 'live component assignment contracts passed'
end program live_component_assignment
