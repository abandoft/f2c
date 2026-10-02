module alternative_scope
  implicit none
  integer, parameter :: shared = 51
end module alternative_scope

module enclosing_scope
  implicit none
  type :: item
    integer :: value = 7
  end type item
  integer, parameter, private :: wide = 8
  integer, parameter :: derived_count = wide / 2
  real, parameter :: factor = 2.5, derived_factor = factor * 2.0
  complex, parameter :: unit_value = (1.0, 2.0), derived_value = unit_value * 2.0
  character(2), parameter :: prefix = 'ab'
  character(4), parameter :: derived_text = prefix // prefix
  integer :: sized(derived_count) = 3
  real :: initialized_real(2, 3) = 1.25
  real :: negative_zero(2) = -0.0
  logical :: initialized_logical(2) = .true.
  complex :: initialized_complex(2) = (2.0, 4.0)
  integer :: empty(2:1) = 3
  character(0) :: empty_text(2) = ''
  character(derived_count) :: text = 'abcd'
  integer :: shared = 11
  integer :: array(3) = [1, 2, 3]
  integer :: implicit_data = 83
  integer :: implicit_common = 89
  integer :: implicit_alias = 97
  type(item) :: object
contains
  subroutine local_objects()
    type(item) :: object
    real :: shared
    integer :: array
    integer(kind=wide) :: checked
    object%value = 23
    shared = 3.5
    array = 17
    checked = 29
    if (object%value /= 23 .or. abs(shared - 3.5) > 0.001) stop 1
    if (array /= 17 .or. checked /= 29) stop 2
  end subroutine local_objects

  subroutine dummy_object(object)
    type(item), intent(inout) :: object
    object%value = object%value + 5
  end subroutine dummy_object

  subroutine local_use()
    use alternative_scope, only: shared
    if (shared /= 51) stop 3
  end subroutine local_use

  subroutine local_storage()
    implicit integer(i)
    integer :: local_value
    integer :: implicit_data
    data implicit_data /41/
    common /scope_storage/ implicit_common
    equivalence(implicit_alias, local_value)
    implicit_common = 43
    implicit_alias = 47
    if (implicit_data /= 41 .or. implicit_common /= 43 .or. local_value /= 47) stop 9
  end subroutine local_storage

  subroutine parameter_scope()
    integer :: wide
    real :: factor
    complex :: unit_value
    character(2) :: prefix
    integer :: work(derived_count)
    wide = 27
    factor = 7.0
    unit_value = (9.0, 10.0)
    prefix = 'zz'
    work = 1
    if (wide /= 27 .or. size(work) /= 4) stop 13
    if (abs(derived_factor - 5.0) > 0.001 .or. abs(factor - 7.0) > 0.001) stop 14
    if (abs(derived_value - (2.0, 4.0)) > 0.001) stop 15
    if (abs(unit_value - (9.0, 10.0)) > 0.001) stop 16
    if (derived_text /= 'abab' .or. prefix /= 'zz') stop 17
  end subroutine parameter_scope

  subroutine imported_storage_scope()
    integer :: derived_count
    derived_count = 9
    if (derived_count /= 9 .or. size(sized) /= 4 .or. len(text) /= 4) stop 18
    if (any(sized /= 3) .or. text /= 'abcd') stop 19
  end subroutine imported_storage_scope

  subroutine scoped_attributes()
    volatile :: shared
    asynchronous :: array
    volatile :: array
    shared = shared + 2
    array(1) = array(1) + 3
  end subroutine scoped_attributes

  subroutine local_type()
    type :: item
      real :: other = 2.5
    end type item
    type(item) :: object
    if (abs(object%other - 2.5) > 0.001) stop 4
  end subroutine local_type

  subroutine nearest_host()
    type(item) :: object
    integer :: shared
    object%value = 31
    shared = 19
    call use_shadowing_reader()
    if (object%value /= 37 .or. shared /= 21) stop 5
  contains
    subroutine use_shadowing_reader()
      use alternative_scope, only: shared
      call update()
      if (shared /= 51) stop 12
    end subroutine use_shadowing_reader
    subroutine update()
      object%value = object%value + 6
      shared = shared + 2
    end subroutine update
  end subroutine nearest_host
end module enclosing_scope

module forwarded_scope
  use enclosing_scope, only: derived_count, derived_text, sized, text
end module forwarded_scope

program scope_association
  use enclosing_scope
  use forwarded_scope
  implicit none
  type(item) :: supplied
  integer :: wide
  integer :: local_work(derived_count)
  wide = 29
  local_work = 2
  call local_objects()
  call dummy_object(supplied)
  call local_use()
  call local_type()
  call nearest_host()
  call local_storage()
  call parameter_scope()
  call imported_storage_scope()
  if (supplied%value /= 12) stop 6
  if (object%value /= 7 .or. shared /= 11) stop 7
  if (any(array /= [1, 2, 3])) stop 8
  if (any(abs(initialized_real - 1.25) > 0.001)) stop 20
  if (sign(1.0, negative_zero(1)) > 0.0) stop 21
  if (.not. all(initialized_logical)) stop 22
  if (any(abs(initialized_complex - (2.0, 4.0)) > 0.001)) stop 23
  if (size(empty) /= 0 .or. size(empty_text) /= 2 .or. len(empty_text) /= 0) stop 24
  if (wide /= 29 .or. size(local_work) /= 4 .or. derived_text /= 'abab') stop 25
  if (implicit_data /= 83 .or. implicit_common /= 89 .or. implicit_alias /= 97) stop 10
  call scoped_attributes()
  if (shared /= 13 .or. any(array /= [4, 2, 3])) stop 11
  print *, 'scope association passed'
end program scope_association
