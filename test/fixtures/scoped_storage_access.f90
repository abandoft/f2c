module scoped_storage_model
  implicit none
  type :: sample
    integer :: count
    real(kind=8) :: value
    complex(kind=8) :: phase
    logical :: ready
    character(len=4) :: text
    integer :: vector(3)
  end type
  integer, volatile :: module_count = 5
  integer, volatile :: module_values(4) = [1, 2, 3, 4]
contains
  subroutine ordinary_integer(value)
    integer, intent(inout) :: value
    value = value + 1
  end subroutine
  integer function ordinary_result(value)
    integer, intent(in) :: value
    ordinary_result = value * 2
  end function
  subroutine qualified_forward(value)
    integer, volatile, intent(inout) :: value
    call ordinary_integer(value)
    value = ordinary_result(value) + 1
  end subroutine
  subroutine ordinary_array(values)
    integer, intent(inout) :: values(:)
    values = values + 2
  end subroutine
  subroutine ordinary_explicit(values)
    integer, intent(inout) :: values(4)
    values = values + 3
  end subroutine
  subroutine ordinary_character(text)
    character(len=*), intent(inout) :: text
    text = 'pass'
  end subroutine
  subroutine qualified_array_forward(values)
    integer, volatile, intent(inout) :: values(:)
    call ordinary_array(values)
    values(1) = values(1) + 1
  end subroutine
  subroutine host_views()
    integer :: value, values(4)
    character(len=4) :: text
    type(sample) :: object
    value = 5
    values = [1,2,3,4]
    text = 'init'
    object%count = 20
    call scoped_view()
    if (value /= 13 .or. any(values /= [3,4,5,6])) stop 17
    if (text /= 'pass' .or. object%count /= 21) stop 18
  contains
    subroutine scoped_view()
      volatile :: value, values, text, object
      call ordinary_integer(value)
      value = value + 7
      call ordinary_array(values)
      call ordinary_character(text)
      call ordinary_integer(object%count)
    end subroutine
  end subroutine
  subroutine automatic_view(n)
    integer, intent(in) :: n
    integer, volatile :: values(n)
    character(len=n), volatile :: text
    values = 1
    call ordinary_array(values)
    call ordinary_character(text)
    if (any(values /= 3) .or. text /= 'pass ') stop 19
  end subroutine
end module

program scoped_storage_access
  use scoped_storage_model
  implicit none
  integer, volatile, target :: scalar, values(4)
  integer, volatile :: associated_scalar, associated_values(4)
  integer, volatile :: equivalent(2), alias(2)
  equivalence (equivalent, alias)
  common /observed_storage/ associated_scalar, associated_values
  real(kind=8), volatile :: real_value
  complex(kind=8), volatile :: complex_value
  logical(kind=1), volatile :: logical_value
  character(len=4), volatile :: text
  character(len=128) :: io_text
  real(kind=8), volatile :: real_values(2)
  integer :: io_status, f2c_qualified_input_0, f2c_qualified_read_0
  type(sample), volatile :: record
  integer, volatile, pointer :: pointer_values(:)
  integer, volatile, allocatable :: allocated_values(:)
  namelist /numeric_values/ real_values
  f2c_qualified_input_0 = 101
  f2c_qualified_read_0 = 202
  scalar = 3
  call ordinary_integer(scalar)
  if (scalar /= 4 .or. ordinary_result(scalar) /= 8) stop 1
  call qualified_forward(scalar)
  if (scalar /= 11) stop 2
  values = [1, 2, 3, 4]
  call ordinary_explicit(values)
  if (any(values /= [4, 5, 6, 7])) stop 3
  call ordinary_array(values(4:1:-1))
  call qualified_array_forward(values)
  if (any(values /= [9, 9, 10, 11])) stop 4
  call ordinary_integer(values(2))
  if (values(2) /= 10) stop 5
  text = 'init'
  call ordinary_character(text)
  if (text /= 'pass') stop 6
  real_value = 1.25_8
  real_value = real_value + 2.0_8
  complex_value = cmplx(1.0_8, 2.0_8, kind=8)
  complex_value = complex_value + cmplx(3.0_8, -1.0_8, kind=8)
  logical_value = .false.
  logical_value = .not. logical_value
  if (real_value /= 3.25_8 .or. real(complex_value,kind=8) /= 4.0_8) stop 7
  if (aimag(complex_value) /= 1.0_8 .or. .not. logical_value) stop 8
  record%count = 12
  record%value = real_value
  record%phase = complex_value
  record%ready = logical_value
  record%text = text
  record%vector = [4, 5, 6]
  call ordinary_integer(record%count)
  call ordinary_integer(record%vector(2))
  call ordinary_character(record%text)
  if (record%count /= 13 .or. record%vector(2) /= 6) stop 9
  if (record%value /= 3.25_8 .or. .not. record%ready) stop 10
  if (record%text /= 'pass' .or. aimag(record%phase) /= 1.0_8) stop 11
  call ordinary_integer(module_count)
  call ordinary_explicit(module_values)
  if (module_count /= 6 .or. any(module_values /= [4,5,6,7])) stop 12
  associated_scalar = 10
  associated_values = [1,2,3,4]
  call ordinary_common()
  call qualified_common()
  if (associated_scalar /= 13 .or. any(associated_values /= [4,5,6,7])) stop 13
  equivalent = [20,30]
  call ordinary_integer(alias(1))
  if (equivalent(1) /= 21) stop 14
  pointer_values => values
  call ordinary_array(pointer_values)
  if (any(values /= [11,12,12,13])) stop 15
  allocate(allocated_values(4))
  allocated_values = [1,2,3,4]
  call ordinary_array(allocated_values)
  if (any(allocated_values /= [3,4,5,6])) stop 16
  deallocate(allocated_values)
  nullify(pointer_values)
  call host_views()
  call automatic_view(5)
  write(io_text, '(4I4)') values
  values = 0
  read(io_text, '(4I4)') values
  if (any(values /= [11,12,12,13])) stop 20
  write(io_text, *) values
  values = 0
  read(io_text, *) values
  if (any(values /= [11,12,12,13])) stop 21
  real_values = [1.25_8, 3.5_8]
  write(io_text, '(2F8.3)') real_values
  real_values = 0.0_8
  read(io_text, '(2F8.3)') real_values
  if (any(real_values /= [1.25_8,3.5_8])) stop 22
  io_text = '   6.250'
  read(io_text, '(F8.3)') real_value
  if (real_value /= 6.25_8) stop 23
  write(io_text, nml=numeric_values)
  real_values = -1.0_8
  read(io_text, nml=numeric_values)
  if (any(real_values /= [1.25_8,3.5_8])) stop 24
  io_text = 'invalid'
  read(io_text, *, iostat=io_status) real_value
  if (io_status <= 0 .or. real_value /= 6.25_8) stop 25
  if (f2c_qualified_input_0 /= 101 .or. f2c_qualified_read_0 /= 202) stop 26
  io_text = '9.5,10.25,(2.5,-3.75)'
  read(io_text, *) record%value, real_values(2), complex_value
  if (record%value /= 9.5_8 .or. real_values(2) /= 10.25_8) stop 27
  if (real(complex_value,kind=8) /= 2.5_8 .or. aimag(complex_value) /= -3.75_8) stop 28
  print '(A)', 'scoped storage access contracts passed'
end program

subroutine ordinary_common()
  implicit none
  integer :: count, values(4)
  common /observed_storage/ count, values
  count = count + 1
  values = values + 1
end subroutine

subroutine qualified_common()
  implicit none
  integer, volatile :: count, values(4)
  common /observed_storage/ count, values
  count = count + 2
  values = values + 2
end subroutine
