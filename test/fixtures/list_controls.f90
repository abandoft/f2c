program list_controls
  implicit none
  character(1024) :: record
  character(300) :: long_record
  character(12) :: text, restored_text
  character(24) :: decimal_mode, round_mode, sign_mode, delim_mode
  integer :: status, integer_value
  integer(kind=4) :: bits4
  integer(kind=8) :: bits8
  real(kind=4) :: small
  real(kind=8) :: large, values(3)
  complex(kind=4) :: pair4
  complex(kind=8) :: pair8
  logical :: flag
  namelist /sample/ large, values, pair8, text, flag

  record = '1,25; -2,5; (3,5; -4,25)'
  read(record, *, decimal='comma') small, large, pair8
  if (abs(small - 1.25) > epsilon(small)) stop 1
  if (abs(large + 2.5d0) > epsilon(large)) stop 2
  if (abs(pair8 - cmplx(3.5d0, -4.25d0, kind=8)) > epsilon(large)) stop 3

  record = '(1,25; 2,5)'
  read(record, *, decimal='comma') pair4
  if (abs(pair4 - cmplx(1.25, 2.5)) > epsilon(small)) stop 4

  record = '0.1'
  read(record, *, round='up') large
  bits8 = transfer(large, bits8)
  write(*, '(Z16.16)') bits8
  read(record, *, round='down') large
  bits8 = transfer(large, bits8)
  write(*, '(Z16.16)') bits8
  record = '-0.1'
  read(record, *, round='up') small
  bits4 = transfer(small, bits4)
  write(*, '(Z8.8)') bits4
  read(record, *, round='down') small
  bits4 = transfer(small, bits4)
  write(*, '(Z8.8)') bits4

  record = '1.000000059604644775390625'
  read(record, *, round='nearest') small
  bits4 = transfer(small, bits4)
  write(*, '(Z8.8)') bits4

  text = 'a,''b"c;d'
  write(record, *, decimal='comma', sign='plus', delim='quote') 12, 1.25d0, pair4, text
  if (index(record, '+12') == 0 .or. index(record, '+1,25') == 0) stop 5
  if (index(record, ';') == 0 .or. index(record, '""') == 0) stop 6
  read(record, *, decimal='comma') integer_value, large, pair4, restored_text
  if (integer_value /= 12 .or. restored_text /= text) stop 7
  write(record, *, delim='apostrophe') text
  if (index(record, "''") == 0) stop 8
  read(record, *) restored_text
  if (restored_text /= text) stop 9
  write(record, *, delim='none') 'ab', 'cd'
  if (trim(adjustl(record)) /= 'abcd') stop 10

  long_record = repeat('0', 280) // '42'
  read(long_record, *) integer_value
  if (integer_value /= 42) stop 11
  long_record = '.T' // repeat('x', 280)
  read(long_record, *) flag
  if (.not. flag) stop 12
  long_record = '.FALSEsuffix'
  read(long_record, *) flag
  if (flag) stop 13

  values = 0.0d0
  record = '&sample large=1,25; values=2*3,5; -4,25; pair8=(2,5; -3,75); ' // &
           'text="comma,semicolon;"; flag=.TRUEsuffix /'
  read(record, nml=sample, decimal='comma', round='nearest', iostat=status)
  if (status /= 0) stop 14
  if (abs(large - 1.25d0) > epsilon(large)) stop 15
  if (abs(values(1) - 3.5d0) > epsilon(large)) stop 16
  if (abs(values(2) - 3.5d0) > epsilon(large)) stop 16
  if (abs(values(3) + 4.25d0) > epsilon(large)) stop 16
  if (abs(pair8 - cmplx(2.5d0, -3.75d0, kind=8)) > epsilon(large)) stop 17
  if (text /= 'comma,semico' .or. .not. flag) stop 18
  write(record, nml=sample, decimal='comma', sign='plus', delim='quote')
  if (index(record, '+1,25') == 0 .or. index(record, '"') == 0) stop 19
  large = 0.0d0
  values = 0.0d0
  text = ''
  flag = .false.
  read(record, nml=sample, decimal='comma')
  if (abs(large - 1.25d0) > epsilon(large) .or. .not. flag) stop 20

  open(24, status='scratch', decimal='comma', round='up', sign='plus', delim='quote')
  write(24, *) 1.25d0, text
  inquire(24, decimal=decimal_mode, round=round_mode, sign=sign_mode, delim=delim_mode)
  if (decimal_mode /= 'COMMA' .or. round_mode /= 'UP') stop 21
  if (sign_mode /= 'PLUS' .or. delim_mode /= 'QUOTE') stop 22
  write(24, *, decimal='point', round='nearest', sign='suppress', delim='apostrophe') 2.5d0
  inquire(24, decimal=decimal_mode, round=round_mode, sign=sign_mode, delim=delim_mode)
  if (decimal_mode /= 'COMMA' .or. round_mode /= 'UP') stop 23
  if (sign_mode /= 'PLUS' .or. delim_mode /= 'QUOTE') stop 24
  rewind(24)
  read(24, '(A)') record
  if (index(record, '+1,25') == 0 .or. index(record, '"') == 0) stop 25
  read(24, '(A)') record
  if (index(record, '2.5') == 0 .or. index(record, '+') /= 0) stop 26
  close(24)
end program list_controls
