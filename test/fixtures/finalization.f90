module lifecycle_state
  implicit none
  integer :: finalized = 0
  integer :: wrapper_finalized = 0
  integer :: parent_finalized = 0
  integer :: child_finalized = 0
  type :: resource
    integer :: value
  contains
    final :: finalize_resource, finalize_resources
  end type resource
  type :: parent_resource
  contains
    final :: finalize_parent, finalize_parent_array
  end type parent_resource
  type :: wrapper
    type(resource) :: nested
  contains
    final :: finalize_wrapper
  end type wrapper
  type, extends(parent_resource) :: child_resource
  contains
    final :: finalize_child, finalize_child_array
  end type child_resource
contains
  subroutine finalize_resource(object)
    type(resource), intent(inout) :: object
    finalized = finalized + 1
    object%value = 0
  end subroutine finalize_resource
  subroutine finalize_resources(objects)
    type(resource), intent(inout) :: objects(:)
    finalized = finalized + 10
  end subroutine finalize_resources
  subroutine finalize_array_scope()
    type(resource) :: values(2)
    values(1)%value = 1
    values(2)%value = 2
  end subroutine finalize_array_scope
  subroutine reset_resource(object)
    type(resource), intent(out) :: object
    object%value = 9
  end subroutine reset_resource
  function make_resource(value) result(object)
    integer, intent(in) :: value
    type(resource) :: object
    object%value = value
  end function make_resource
  subroutine finalize_wrapper(object)
    type(wrapper), intent(inout) :: object
    wrapper_finalized = wrapper_finalized + 1
  end subroutine finalize_wrapper
  subroutine finalize_parent_array(objects)
    type(parent_resource), intent(inout) :: objects(:)
    parent_finalized = parent_finalized + 1000
  end subroutine finalize_parent_array
  subroutine finalize_parent(object)
    type(parent_resource), intent(inout) :: object
    parent_finalized = parent_finalized + 1
  end subroutine finalize_parent
  subroutine finalize_child_array(objects)
    type(child_resource), intent(inout) :: objects(:)
    child_finalized = child_finalized + 100
  end subroutine finalize_child_array
  subroutine finalize_child(object)
    type(child_resource), intent(inout) :: object
    child_finalized = child_finalized + 1
  end subroutine finalize_child
  subroutine finalize_inherited_array_scope()
    type(child_resource) :: objects(2)
  end subroutine finalize_inherited_array_scope
end module lifecycle_state

program finalization
  use lifecycle_state
  implicit none
  type(resource) :: source
  type(resource) :: target
  type(wrapper) :: wrapper_source
  type(wrapper) :: wrapper_target

  source%value = 42
  target = source
  if (target%value /= 42) stop 1
  if (finalized /= 1) stop 2
  call finalize_array_scope()
  if (finalized /= 11) stop 3
  call reset_resource(source)
  if (source%value /= 9) stop 4
  if (finalized /= 12) stop 5
  target = make_resource(77)
  if (target%value /= 77) stop 6
  if (finalized /= 14) stop 7
  wrapper_source%nested%value = 91
  wrapper_target = wrapper_source
  if (wrapper_target%nested%value /= 91) stop 8
  if (wrapper_finalized /= 1) stop 9
  if (finalized /= 15) stop 10
  call finalize_inherited_array_scope()
  if (child_finalized /= 100) stop 11
  if (parent_finalized /= 1000) stop 12
end program finalization
