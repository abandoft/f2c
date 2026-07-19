program record_io
  implicit none
  type :: payload
    integer :: identifier
    real :: samples(2)
    character(4) :: name
  end type payload
  integer, parameter :: sequential_unit = 41
  integer, parameter :: direct_formatted_unit = 42
  integer, parameter :: direct_unformatted_unit = 43
  integer, parameter :: write_only_unit = 44
  integer :: status, value, next_record
  integer(kind=1) :: tiny_value, actual_tiny
  integer(kind=2) :: short_value, actual_short
  integer(kind=8) :: wide_value, actual_wide
  integer :: values(3), actual_values(3)
  real :: real_value, actual_real
  double precision :: double_value, actual_double
  complex :: complex_value, actual_complex
  complex(kind=8) :: double_complex_value, actual_double_complex
  logical :: flag, actual_flag
  logical(kind=1) :: compact_flag, actual_compact_flag
  character(5) :: text, actual_text
  character(3) :: labels(2), actual_labels(2)
  character(64) :: message
  character(64) :: formatted_buffer
  type(payload) :: expected_payload, actual_payload

  values = [11, -22, 33]
  tiny_value = -12
  short_value = 1234
  wide_value = 1234567890123_8
  real_value = 1.25
  double_value = -9.125_8
  complex_value = (2.5, -3.75)
  double_complex_value = (7.5_8, -8.25_8)
  flag = .true.
  compact_flag = .true.
  text = 'abcde'
  labels(1) = 'one'
  labels(2) = 'two'
  expected_payload%identifier = 77
  expected_payload%samples(1) = 0.5
  expected_payload%samples(2) = -1.5
  expected_payload%name = 'data'

  formatted_buffer = ''
  write(formatted_buffer, '(I3,1X,F4.1,1X,F4.1,1X,A4)') expected_payload
  actual_payload%identifier = 0
  actual_payload%samples(1) = 0.0
  actual_payload%samples(2) = 0.0
  actual_payload%name = '----'
  read(formatted_buffer, '(I3,1X,F4.1,1X,F4.1,1X,A4)') actual_payload
  if (actual_payload%identifier /= expected_payload%identifier) error stop
  if (abs(actual_payload%samples(1) - expected_payload%samples(1)) > epsilon(real_value)) &
       error stop
  if (abs(actual_payload%samples(2) - expected_payload%samples(2)) > epsilon(real_value)) &
       error stop
  if (actual_payload%name /= expected_payload%name) error stop

  formatted_buffer = ''
  write(formatted_buffer, *) expected_payload
  actual_payload%identifier = 0
  actual_payload%samples(1) = 0.0
  actual_payload%samples(2) = 0.0
  actual_payload%name = '----'
  read(formatted_buffer, *) actual_payload
  if (actual_payload%identifier /= expected_payload%identifier) error stop
  if (abs(actual_payload%samples(1) - expected_payload%samples(1)) > epsilon(real_value)) &
       error stop
  if (abs(actual_payload%samples(2) - expected_payload%samples(2)) > epsilon(real_value)) &
       error stop
  if (actual_payload%name /= expected_payload%name) error stop

  open(unit=sequential_unit, file='f2c-record-sequential.tmp', status='replace', &
       access='sequential', form='unformatted', action='readwrite', &
       iostat=status, iomsg=message)
  if (status /= 0) error stop
  write(sequential_unit, iostat=status, iomsg=message) values, real_value, &
       complex_value, flag, text, labels, expected_payload, tiny_value, short_value, &
       wide_value, double_value, double_complex_value, compact_flag
  if (status /= 0) error stop
  write(sequential_unit, iostat=status, iomsg=message) 9876
  if (status /= 0) error stop
  rewind(sequential_unit, iostat=status, iomsg=message)
  if (status /= 0) error stop

  actual_values = 0
  actual_real = 0.0
  actual_complex = (0.0, 0.0)
  actual_flag = .false.
  actual_text = '-----'
  actual_labels(1) = '---'
  actual_labels(2) = '---'
  actual_payload%identifier = 0
  actual_payload%samples(1) = 0.0
  actual_payload%samples(2) = 0.0
  actual_payload%name = '----'
  actual_tiny = 0
  actual_short = 0
  actual_wide = 0
  actual_double = 0.0_8
  actual_double_complex = (0.0_8, 0.0_8)
  actual_compact_flag = .false.
  read(sequential_unit, iostat=status, iomsg=message) actual_values, actual_real, &
       actual_complex, actual_flag, actual_text, actual_labels, actual_payload, actual_tiny, &
       actual_short, actual_wide, actual_double, actual_double_complex, actual_compact_flag
  if (status /= 0) error stop
  if (any(actual_values /= values)) error stop
  if (abs(actual_real - real_value) > epsilon(real_value)) error stop
  if (abs(actual_complex - complex_value) > epsilon(real_value)) error stop
  if (actual_flag .neqv. flag .or. actual_text /= text) error stop
  if (any(actual_labels /= labels)) error stop
  if (actual_payload%identifier /= expected_payload%identifier) error stop
  if (abs(actual_payload%samples(1) - expected_payload%samples(1)) > epsilon(real_value)) error stop
  if (abs(actual_payload%samples(2) - expected_payload%samples(2)) > epsilon(real_value)) error stop
  if (actual_payload%name /= expected_payload%name) error stop
  if (actual_tiny /= tiny_value .or. actual_short /= short_value) error stop
  if (actual_wide /= wide_value) error stop
  if (abs(actual_double - double_value) > epsilon(double_value)) error stop
  if (abs(actual_double_complex - double_complex_value) > epsilon(double_value)) error stop
  if (actual_compact_flag .neqv. compact_flag) error stop
  read(sequential_unit, iostat=status, iomsg=message) value
  if (status /= 0 .or. value /= 9876) error stop
  backspace(sequential_unit, iostat=status, iomsg=message)
  if (status /= 0) error stop
  value = 0
  read(sequential_unit, iostat=status, iomsg=message) value
  if (status /= 0 .or. value /= 9876) error stop
  read(sequential_unit, iostat=status, iomsg=message) value
  if (status >= 0) error stop
  close(sequential_unit, status='delete', iostat=status, iomsg=message)
  if (status /= 0) error stop

  open(unit=direct_formatted_unit, file='f2c-record-formatted.tmp', status='replace', &
       access='direct', form='formatted', recl=16, action='readwrite', &
       iostat=status, iomsg=message)
  if (status /= 0) error stop
  write(direct_formatted_unit, '(I4,1X,A5)', rec=2, iostat=status, iomsg=message) 42, 'hello'
  if (status /= 0) error stop
  write(direct_formatted_unit, '(I4)', rec=1, iostat=status, iomsg=message) -7
  if (status /= 0) error stop
  write(direct_formatted_unit, '(I4/I4)', rec=4, iostat=status, iomsg=message) 111, 222
  if (status /= 0) error stop 101
  write(direct_formatted_unit, '(I4)', rec=6, iostat=status, iomsg=message) 333, 444
  if (status /= 0) error stop 102
  value = 0
  actual_text = '-----'
  read(direct_formatted_unit, '(I4,1X,A5)', rec=2, iostat=status, iomsg=message) value, actual_text
  if (status /= 0 .or. value /= 42 .or. actual_text /= 'hello') error stop
  actual_values = 0
  read(direct_formatted_unit, '(I4/I4)', rec=4, iostat=status, iomsg=message) &
       actual_values(1), actual_values(2)
  if (status /= 0 .or. actual_values(1) /= 111 .or. actual_values(2) /= 222) error stop 103
  actual_values = 0
  read(direct_formatted_unit, '(I4)', rec=6, iostat=status, iomsg=message) &
       actual_values(1), actual_values(2)
  if (status /= 0 .or. actual_values(1) /= 333 .or. actual_values(2) /= 444) error stop 104
  inquire(unit=direct_formatted_unit, nextrec=next_record, iostat=status, iomsg=message)
  if (status /= 0 .or. next_record /= 8) error stop 105
  read(direct_formatted_unit, '(I4)', iostat=status, iomsg=message) value
  if (status <= 0) error stop 106
  read(direct_formatted_unit, rec=1, iostat=status, iomsg=message) value
  if (status <= 0) error stop
  close(direct_formatted_unit, status='delete', iostat=status, iomsg=message)
  if (status /= 0) error stop

  open(unit=direct_unformatted_unit, file='f2c-record-unformatted.tmp', status='replace', &
       access='direct', form='unformatted', recl=64, action='readwrite', &
       iostat=status, iomsg=message)
  if (status /= 0) error stop
  write(direct_unformatted_unit, rec=3, iostat=status, iomsg=message) values, flag, text
  if (status /= 0) error stop
  actual_values = 0
  actual_flag = .false.
  actual_text = '-----'
  read(direct_unformatted_unit, rec=3, iostat=status, iomsg=message) &
       actual_values, actual_flag, actual_text
  if (status /= 0 .or. any(actual_values /= values)) error stop
  if (actual_flag .neqv. flag .or. actual_text /= text) error stop
  read(direct_unformatted_unit, iostat=status, iomsg=message) value
  if (status <= 0) error stop
  actual_wide = 0_8
  read(direct_unformatted_unit, rec=actual_wide, iostat=status, iomsg=message) value
  if (status <= 0) error stop
  actual_wide = 9223372036854775807_8
  read(direct_unformatted_unit, rec=actual_wide, iostat=status, iomsg=message) value
  if (status <= 0) error stop
  close(direct_unformatted_unit, status='delete', iostat=status, iomsg=message)
  if (status /= 0) error stop

  open(unit=write_only_unit, status='scratch', action='write', form='formatted', &
       iostat=status, iomsg=message)
  if (status /= 0) error stop 107
  read(write_only_unit, *, iostat=status, iomsg=message) value
  if (status <= 0) error stop 108
  close(write_only_unit, iostat=status, iomsg=message)
  if (status /= 0) error stop 109

  write(*, '(A)') 'record-io ok'
end program record_io
