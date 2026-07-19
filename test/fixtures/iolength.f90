module iolength_state
  implicit none
  integer :: evaluation_count = 0
contains
  complex function evaluated_complex()
    evaluation_count = evaluation_count + 1
    evaluated_complex = (2.0, -3.0)
  end function evaluated_complex

  integer function evaluated_offset()
    evaluation_count = evaluation_count + 1
    evaluated_offset = 5
  end function evaluated_offset

  integer function evaluated_lower()
    evaluation_count = evaluation_count + 1
    evaluated_lower = 2
  end function evaluated_lower
end module iolength_state

program iolength
  use iolength_state
  implicit none
  type :: payload
    integer :: identifier
    real :: samples(2)
    character(3) :: name
  end type payload
  integer, parameter :: direct_unit = 61
  integer :: record_length, expression_length, iterator, status
  integer(kind=8) :: wide_length
  integer(kind=1) :: tiny_length
  integer(kind=2) :: short_length
  integer :: values(4), reversed(4), loop_values(3), constructor_values(3)
  integer :: expression_values(4), selected_values(2)
  integer :: indices(2)
  real :: real_value
  complex :: complex_value, restored_complex
  logical :: flag, restored_flag
  character(5) :: text, restored_text
  character(2) :: labels(3), restored_labels(3)
  character(64) :: message
  type(payload) :: object, restored_object
  integer, allocatable :: empty(:)

  values = [10, 20, 30, 40]
  indices = [2, 4]
  real_value = 1.25
  complex_value = (4.5, -6.25)
  flag = .true.
  text = 'hello'
  labels = ['aa', 'bb', 'cc']
  object%identifier = 77
  object%samples(1) = 0.5
  object%samples(2) = -1.5
  object%name = 'obj'

  inquire(iolength=expression_length) evaluated_complex()
  if (evaluation_count /= 1 .or. expression_length <= 0) error stop 1
  evaluation_count = 0
  inquire(iolength=expression_length) values + evaluated_offset()
  if (evaluation_count /= 1 .or. expression_length <= 0) error stop 1
  evaluation_count = 0
  inquire(iolength=expression_length) values(evaluated_lower():4)
  if (evaluation_count /= 1 .or. expression_length <= 0) error stop 1
  inquire(iolength=wide_length) values
  inquire(iolength=tiny_length) real_value
  inquire(iolength=short_length) complex_value
  if (wide_length <= 0_8 .or. tiny_length <= 0 .or. short_length <= 0) error stop 1
  allocate(empty(1:0))
  inquire(iolength=expression_length) empty
  if (expression_length /= 0) error stop 1
  deallocate(empty)

  inquire(iolength=record_length) values(4:1:-1), &
       (iterator * 3, iterator=1, 3), [7, 8, 9], values + 10, values(indices), &
       real_value, complex_value, flag, text, labels(3:1:-1), object
  if (record_length <= 0) error stop 2

  open(unit=direct_unit, file='f2c-iolength.tmp', status='replace', access='direct', &
       form='unformatted', action='readwrite', recl=record_length, &
       iostat=status, iomsg=message)
  if (status /= 0) error stop 3
  write(direct_unit, rec=1, iostat=status, iomsg=message) values(4:1:-1), &
       (iterator * 3, iterator=1, 3), [7, 8, 9], values + 10, values(indices), &
       real_value, complex_value, flag, text, labels(3:1:-1), object
  if (status /= 0) error stop 4

  reversed = 0
  loop_values = 0
  constructor_values = 0
  expression_values = 0
  selected_values = 0
  restored_complex = (0.0, 0.0)
  restored_flag = .false.
  restored_text = '-----'
  restored_labels = '--'
  restored_object%identifier = 0
  restored_object%samples(1) = 0.0
  restored_object%samples(2) = 0.0
  restored_object%name = '---'
  read(direct_unit, rec=1, iostat=status, iomsg=message) reversed, loop_values, &
       constructor_values, expression_values, selected_values, real_value, restored_complex, &
       restored_flag, restored_text, restored_labels, restored_object
  if (status /= 0) error stop 5
  if (any(reversed /= [40, 30, 20, 10])) error stop 6
  if (any(loop_values /= [3, 6, 9])) error stop 7
  if (any(constructor_values /= [7, 8, 9])) error stop 8
  if (any(expression_values /= [20, 30, 40, 50])) error stop 9
  if (any(selected_values /= [20, 40])) error stop 10
  if (abs(real_value - 1.25) > epsilon(real_value)) error stop 11
  if (abs(restored_complex - complex_value) > epsilon(real_value)) error stop 12
  if (restored_flag .neqv. flag .or. restored_text /= text) error stop 13
  if (any(restored_labels /= ['cc', 'bb', 'aa'])) error stop 14
  if (restored_object%identifier /= object%identifier) error stop 15
  if (abs(restored_object%samples(1) - object%samples(1)) > epsilon(real_value)) error stop 16
  if (abs(restored_object%samples(2) - object%samples(2)) > epsilon(real_value)) error stop 16
  if (restored_object%name /= object%name) error stop 17

  close(direct_unit, status='delete', iostat=status, iomsg=message)
  if (status /= 0) error stop 18
  write(*, '(A)') 'iolength ok'
end program iolength
