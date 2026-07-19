module dependent_types
  use provider_types, only: local_record => record_type
  implicit none
  type(local_record) :: stored
end module dependent_types

module provider_types
  implicit none
  type :: record_type
    integer :: value
  end type record_type
end module provider_types

program module_dependency_order
  use dependent_types, only: stored
  implicit none
  stored%value = 17
  if (stored%value /= 17) error stop 1
end program module_dependency_order
