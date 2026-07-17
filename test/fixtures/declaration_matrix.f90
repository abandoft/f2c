program declaration_matrix_test
  implicit none
  integer, parameter :: rk = kind(1.0d0)
  real(kind=rk), dimension(2) :: values
  real*8 :: legacy_real
  complex*16 :: legacy_complex
  character(len=3, kind=1) :: words(2)
  character :: legacy*4

  values = [1.25_rk, 2.5_rk]
  legacy_real = sum(values)
  legacy_complex = (1.0_rk, 2.0_rk)
  words = ['abc', 'xyz']
  legacy = 'test'

  if (abs(legacy_real - 3.75_rk) > 1.0e-12_rk) stop 1
  if (abs(real(legacy_complex, kind=rk) - 1.0_rk) > 1.0e-12_rk) stop 2
  if (words(2) /= 'xyz') stop 3
  if (legacy /= 'test') stop 4
end program declaration_matrix_test
